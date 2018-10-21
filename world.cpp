#include "server.hpp"

/* Chunk class functions */

Chunk::Chunk(const int32_t cx, const int32_t cy, const uint32_t bgclr, Database * const db)
	: db(db),
	  bgclr(bgclr),
	  cx(cx),
	  cy(cy),
	  changed(false),
	  ranked(db->getChunkProtection(cx, cy)) {
	if(!db->get_chunk(cx, cy, (char *)&data)){
		for (size_t i = 0; i < sizeof(data); i++) {
			data[i] = (uint8_t) (bgclr >> ((i % 3) * 8));
		}
		//memset(data, 255, sizeof(data)); // infra req: change color
	}
}

Chunk::~Chunk() {
	save();
}

bool Chunk::set_data(const uint8_t x, const uint8_t y, const RGB clr) {
	const uint16_t pos = (y * 16 + x) * 3;
	if(data[pos] == clr.r && data[pos + 1] == clr.g && data[pos + 2] == clr.b){
		return false;
	}
	data[pos] = clr.r;
	data[pos + 1] = clr.g;
	data[pos + 2] = clr.b;
	changed = true;
	return true;
}

size_t Chunk::compress_data_to(uint8_t (&msg)[16 * 16 * 3 + 10 + 4]) {
	const uint16_t s = 16 * 16 * 3;
	struct compressedPoint {
		uint16_t pos;
		uint16_t length;
	};
	std::vector<compressedPoint> compressedPos;
	uint16_t compBytes = 3;
	uint32_t lastclr = data[2] << 16 | data[1] << 8 | data[0];
	uint16_t t = 1;
	for (uint16_t i = 3; i < sizeof(data); i += 3) {
		uint32_t clr = data[i + 2] << 16 | data[i + 1] << 8 | data[i];
		compBytes += 3;
		
		if (clr == lastclr) {
			++t;
		} else {
			if (t >= 3) {
				compBytes -= t * 3 + 3;
				compressedPos.push_back({compBytes, t});
				compBytes += 5 + 3;
			}
			lastclr = clr;
			t = 1;
		}
	}
	
	if (t >= 3) {
		compBytes -= t * 3;
		compressedPos.push_back({compBytes, t});
		compBytes += 5;
	}
	
	const uint16_t totalcareas = compressedPos.size();
	//std::cout << compBytes + totalcareas * 2 << std::endl;
	msg[0] = CHUNKDATA;
	memcpy(&msg[1], &cx, 4);
	memcpy(&msg[5], &cy, 4);
	memcpy(&msg[9], &ranked, 1);
	uint8_t * curr = &msg[10];
	memcpy(curr, &s, sizeof(uint16_t));
	curr += sizeof(uint16_t);
	memcpy(curr, &totalcareas, sizeof(uint16_t));
	curr += sizeof(uint16_t);
	for (auto point : compressedPos) {
		memcpy(curr, &point.pos, sizeof(uint16_t));
		curr += sizeof(uint16_t);
	}
	size_t di = 0;
	size_t ci = 0;
	for (auto point : compressedPos) {
		while (ci < point.pos) {
			curr[ci++] = data[di++];
		}
		memcpy(curr + ci, &point.length, sizeof(uint16_t));
		ci += sizeof(uint16_t);
		curr[ci++] = data[di++];
		curr[ci++] = data[di++];
		curr[ci++] = data[di++];
		di += point.length * 3 - 3;
	}
	while (di < s) {
		curr[ci++] = data[di++];
	}
	return compBytes + totalcareas * 2 + 10 + 2 + 2;
}

uWS::WebSocket<uWS::SERVER>::PreparedMessage * Chunk::get_prepd_data_msg() {
	uint8_t msg[16 * 16 * 3 + 10 + 4];
	size_t size = compress_data_to(msg);
	uWS::WebSocket<uWS::SERVER>::PreparedMessage * prep = uWS::WebSocket<uWS::SERVER>::prepareMessage(
			(char *) &msg[0], size, uWS::BINARY, false);
	return prep;
}

void Chunk::send_data(uWS::WebSocket<uWS::SERVER> ws, bool compressed) {
	uint8_t msg[16 * 16 * 3 + 10 + 4];
	size_t size = compress_data_to(msg);
	ws.send((const char *)&msg[0], size, uWS::BINARY);
}

uint8_t * Chunk::get_data() {
	return data;
}

void Chunk::set_data(char const * const newdata, size_t size) {
	memcpy(data, newdata, size);
	changed = true;
}

void Chunk::save() {
	if(changed){
		changed = false;
		db->set_chunk(cx, cy, (char *)&data);
		/* std::cout << "Chunk saved at X: " << cx << ", Y: " << cy << std::endl; */
	}
}

void Chunk::clear(){
	for (size_t i = 0; i < sizeof(data); i++) {
		data[i] = (uint8_t) (bgclr >> ((i % 3) * 8));
	}
	changed = true;
}

/* World class functions */

World::World(const std::string& path, const std::string& name)
	: bgclr(0xFFFFFF),
	  pids(0),
	  paintrate(32),
	  defaultRank(Client::USER),
	  db(path + name + "/"),
	  pass(),
	  name(name) {
	uv_timer_init(uv_default_loop(), &upd_hdl);
	upd_hdl.data = this;
	reload();
}

World::~World() {
	for(const auto& chunk : chunks){
		delete chunk.second;
	}
	std::cout << "World unloaded: " << name << std::endl;
}

std::string World::getProp(std::string key, std::string defval) {
	return db.getProp(key, defval);
}

void World::setProp(std::string key, std::string value) {
	db.setProp(key, value);
}

void World::reload() {
	pass = getProp("password");
	try {
		uint32_t r = stoul(getProp("paintrate", "32"));
		/*uint32_t p = stoul(getProp("paintper", "0"));*/
		r = r > 0xFFFF ? 0xFFFF : r;
		paintrate = r;
		bgclr = stoul(getProp("bgcolor", "FFFFFF"), nullptr, 16);
		bgclr = (bgclr & 0xFF) << 16 | (bgclr & 0xFF00) | (bgclr & 0xFF0000) >> 16;
	} catch(std::invalid_argument) {
		broadcast("DEVException while reloading world properties. (std::invalid_argument)");
	} catch(std::out_of_range) {
		broadcast("DEVException while reloading world properties. (std::out_of_range)");
	}
}

void World::update_all_clients() {
	for (auto cli : clients) {
		plupdates.emplace(cli);
	}
}

uint32_t World::get_id() {
	return ++pids;
}

void World::add_cli(Client * const cl) {
	const std::string motd(getProp("motd"));
	cl->tell("*<span><style>@import url(//fonts.googleapis.com/css?family=Play);#custom-motd{display:flex;flex-direction:column;padding:16px;font-family:Play,serif;font-size:12pt;color:#dedede}#custom-motd>.title{color:#00e6e6;text-align:center;font-size:16pt;font-family:Play}#custom-motd>.large{font-size:27px}#custom-motd>.links{display:flex;justify-content:space-around}#custom-motd>.vertical{flex-direction:column}#custom-motd>*>a{padding:8px;text-align:center;flex:1 auto;color:#ff6000}#custom-motd>#rules{padding:.5em}#custom-motd>*>a:visited{color:#d35400}#custom-motd>*>a:hover{color:#ffc000}#custom-motd>p{padding:.5em;text-align:center}#rules>ol{list-style:none;white-space:nowrap}#rules>ol>li{counter-increment:item;white-space:nowrap}#rules>ol>li:before{content:counter(item) \".\";margin-right:.5em;white-space:nowrap;text-shadow:-1px 0 #000,0 1px #000,1px 0 #000,0 -1px #000}</style><div id=custom-motd><span class=\"title large\">Bop It's OWOP</span><span class=title>Rules</span><div id=rules><ol><li>No Retards</ol></div><div class=\"links vertical\"></div>To get admin, join the discord server: discord.gg/wH8Rgyt</span>");
	/*if (motd.size()) {
		cl->tell(motd);
	}*/
	if (!pass.size()) {
		cl->promote(defaultRank, paintrate);
	} else {
		cl->tell("[Server] This world has a password set. Use '/pass PASSWORD' to unlock drawing.");
		cl->promote(Client::NONE, paintrate);
	}
	clients.emplace(cl);
	update_all_clients();
	sched_updates();
}

void World::upd_cli(Client * const cl) {
	plupdates.emplace(cl);
	sched_updates();
}

void World::rm_cli(Client * const cl) {
	plleft.emplace(cl->id);
	clients.erase(cl);
	plupdates.erase(cl);
	if(!clients.size()){
		return;
	}
	sched_updates();
}

Client * World::get_cli(const uint32_t id) const {
	for(const auto client : clients){
		if(id == client->id){
			return client;
		}
	}
	return nullptr;
}

Client * World::get_cli(const std::string name) const {
	for(const auto client : clients){
		if(name == client->get_nick()){
			return client;
		}
	}
	return nullptr;
}

void World::sched_updates() {
	if(!uv_is_active((uv_handle_t *)&upd_hdl)){
		uv_timer_start(&upd_hdl, (uv_timer_cb)&send_updates, WORLD_UPDATE_RATE_MSEC, 0);
	}
}

void World::send_updates(uv_timer_t * const t) {
	World * const wrld = (World *) t->data;
	size_t offs = 2;
	uint32_t tmp;
	uint8_t * const upd = (uint8_t *) malloc(1 + 1 + wrld->plupdates.size() * (sizeof(uint32_t) + sizeof(pinfo_t))
	                                   + sizeof(uint16_t) + wrld->pxupdates.size() * sizeof(pixupd_t)
	                                   + 1 + sizeof(uint32_t) * wrld->plleft.size());
	upd[0] = UPDATE;
	
	bool pendingUpdates = false;
	
	tmp = 0;
	for (auto it = wrld->plupdates.begin();;) {
		if (it == wrld->plupdates.end()) {
			wrld->plupdates.clear();
			break;
		}
		if(tmp >= WORLD_MAX_PLAYER_UPDATES){
			wrld->plupdates.erase(wrld->plupdates.begin(), it);
			pendingUpdates = true;
			break;
		}
		auto client = *it;
		memcpy((void *)(upd + offs), (void *)&client->id, sizeof(uint32_t));
		offs += sizeof(uint32_t);
		memcpy((void *)(upd + offs), (void *)client->get_pos(), sizeof(pinfo_t));
		offs += sizeof(pinfo_t);
		++it;
		++tmp;
	}
	upd[1] = tmp;
	tmp = wrld->pxupdates.size();
	tmp = tmp >= WORLD_MAX_PIXEL_UPDATES ? WORLD_MAX_PIXEL_UPDATES : tmp;
	memcpy((void *)(upd + offs), &tmp, sizeof(uint16_t));
	tmp = 0;
	offs += sizeof(uint16_t);
	for(auto& px : wrld->pxupdates){
		memcpy((void *)(upd + offs), &px, sizeof(pixupd_t));
		offs += sizeof(pixupd_t);
		if(++tmp >= WORLD_MAX_PIXEL_UPDATES){
			break;
		}
	}
	wrld->pxupdates.clear();
	
	tmp = wrld->plleft.size();
	tmp = tmp >= WORLD_MAX_PLAYER_LEFT_UPDATES ? WORLD_MAX_PLAYER_LEFT_UPDATES : tmp;
	memcpy((void *)(upd + offs), &tmp, sizeof(uint8_t));
	tmp = 0;
	offs += sizeof(uint8_t);
	for (auto it = wrld->plleft.begin();;) {
		if (it == wrld->plleft.end()) {
			wrld->plleft.clear();
			break;
		}
		if(tmp >= WORLD_MAX_PLAYER_LEFT_UPDATES){
			wrld->plleft.erase(wrld->plleft.begin(), it);
			pendingUpdates = true;
			break;
		}
		uint32_t pl = *it;
		memcpy((void *)(upd + offs), &pl, sizeof(uint32_t));
		offs += sizeof(uint32_t);
		++tmp;
		++it;
	}
	
	uWS::WebSocket<uWS::SERVER>::PreparedMessage * prep = uWS::WebSocket<uWS::SERVER>::prepareMessage(
		(char *)upd, offs, uWS::BINARY, false);
	for(auto client : wrld->clients){
		client->get_ws().sendPrepared(prep);
	}
	uWS::WebSocket<uWS::SERVER>::finalizeMessage(prep);
	free(upd);
	if (pendingUpdates) {
		wrld->sched_updates();
	}
}

Chunk * World::get_chunk(const int32_t x, const int32_t y, bool create) {
	if(x > WORLD_MAX_CHUNK_XY || y > WORLD_MAX_CHUNK_XY
	  || x < ~WORLD_MAX_CHUNK_XY || y < ~WORLD_MAX_CHUNK_XY){
		return nullptr;
	}
	Chunk * chunk = nullptr;
	const auto search = chunks.find(key(x, y));
	if(search == chunks.end()){
		if(chunks.size() > WORLD_MAX_CHUNKS_LOADED){
			auto it = chunks.begin();
			/* expensive, need to figure out another way of limiting loaded chunks? */
			for(auto it2 = chunks.begin(); ++it2 != chunks.end(); ++it);
			delete it->second;
			chunks.erase(it);
		}
		chunk = chunks[key(x, y)] = new Chunk(x, y, bgclr, &db);
	} else {
		chunk = search->second;
	}
	return chunk;
}

void World::send_chunk(uWS::WebSocket<uWS::SERVER> ws, const int32_t x, const int32_t y, bool compressed) {
	Chunk * const c = get_chunk(x, y);
	if(c){ c->send_data(ws, compressed); }
}

void World::del_chunk(const int32_t x, const int32_t y){
	Chunk * const c = get_chunk(x, y);
	if(c){
		c->clear();
		uWS::WebSocket<uWS::SERVER>::PreparedMessage * prep = c->get_prepd_data_msg();
		for(auto client : clients){
			client->get_ws().sendPrepared(prep);
		}
		uWS::WebSocket<uWS::SERVER>::finalizeMessage(prep);
	}
}

void World::paste_chunk(const int32_t x, const int32_t y, char const * const data){
	Chunk * const c = get_chunk(x, y);
	if(c){
		c->set_data(data, 16 * 16 * 3);

		uWS::WebSocket<uWS::SERVER>::PreparedMessage * prep = c->get_prepd_data_msg();
		for(auto client : clients){
			client->get_ws().sendPrepared(prep);
		}
		uWS::WebSocket<uWS::SERVER>::finalizeMessage(prep);
	}
}

bool World::put_px(const int32_t x, const int32_t y, const RGB clr, uint8_t placerRank) {
	Chunk * const chunk = get_chunk(x >> 4, y >> 4);
	if(chunk && chunk->set_data(x & 0xF, y & 0xF, clr)){
		pxupdates.push_back({x, y, clr.r, clr.g, clr.b});
		sched_updates();
		return true;
	}
	return false;
}

void World::setChunkProtection(int32_t x, int32_t y, bool state) {
	db.setChunkProtection(x, y, state);
	Chunk * c = get_chunk(x, y);
	if (c) {
		c->ranked = state;
		uint8_t msg[10] = {CHUNK_PROTECTED};
		memcpy(&msg[1], (char *)&x, 4);
		memcpy(&msg[5], (char *)&y, 4);
		memcpy(&msg[9], (char *)&state, 1);
		uWS::WebSocket<uWS::SERVER>::PreparedMessage * prep = uWS::WebSocket<uWS::SERVER>::prepareMessage(
			(char *)&msg[0], sizeof(msg), uWS::BINARY, false);
		for(auto client : clients){
			client->get_ws().sendPrepared(prep);
		}
		uWS::WebSocket<uWS::SERVER>::finalizeMessage(prep);
	}
}

void World::safedelete() {
	uv_timer_stop(&upd_hdl);
	uv_close((uv_handle_t *)&upd_hdl, (uv_close_cb)([](uv_handle_t * const t){
		delete (World *)t->data;
	}));
}

void World::broadcast(const std::string& msg) const {
	uWS::WebSocket<uWS::SERVER>::PreparedMessage * prep = uWS::WebSocket<uWS::SERVER>::prepareMessage(
		(char *)msg.c_str(), msg.size(), uWS::TEXT, false);
	for(auto client : clients){
		client->get_ws().sendPrepared(prep);
	}
	uWS::WebSocket<uWS::SERVER>::finalizeMessage(prep);
}

void World::save() {
	for(const auto& chunk : chunks){
		chunk.second->save();
	}
	db.save();
}

bool World::is_empty() const {
	return !clients.size();
}

bool World::is_pass(std::string const& p) const {
	return p == pass;
}

uint8_t World::get_default_rank() {
	return defaultRank;
}

uint16_t World::get_paintrate() {
	return paintrate;
}

void World::set_default_rank(uint8_t r) {
	defaultRank = r;
}

std::set<Client *> * World::get_pl() {
	return &clients;
}
