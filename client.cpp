#include "server.hpp"

#include <cmath>

/* Client class functions */

Client::Client(const uint32_t id, uWS::WebSocket<uWS::SERVER> ws, World * const wrld, SocketInfo * si)
		: nick(),
		  pixupdlimit(0, 1),
		  chatlimit(CLIENT_CHAT_RATELIMIT),
		  ws(ws),
		  wrld(wrld),
		  penalty(0),
		  handledelete(true),
		  rank(1),
		  stealthadmin(false),
		  suspicious(si->origin != "http://ourworldofpixels.com"),
		  compressionEnabled(false),
		  pos({0, 0, 0, 0, 0, 0}),
		  lastclr({0, 0, 0}),
		  id(id),
		  si(si),
		  mute(false),
		  chathtml(false){
	std::cout << "(" << wrld->name << "/" << si->ip << ") New client! ID: " << id << std::endl;
	uv_timer_init(uv_default_loop(), &idletimeout_hdl);
	idletimeout_hdl.data = this;
	//uv_timer_start(&idletimeout_hdl, (uv_timer_cb) &Client::idle_timeout, 300000, 1200000);
	uint8_t msg[5] = {SET_ID};
	memcpy(&msg[1], (char *)&id, sizeof(id));
	ws.send((const char *)&msg, sizeof(msg), uWS::BINARY);
}

Client::~Client() {
	/* std::cout << "Client deleted! ID: " << id << std::endl; */
}

bool Client::can_edit() {
	return (rank = USER);
}

void Client::get_chunk(const int32_t x, const int32_t y) const {
	wrld->send_chunk(ws, x, y);
}

void Client::put_px(const int32_t x, const int32_t y, const RGB clr) {
	
		uint32_t distx = (x >> 4) - (pos.x >> 8); distx *= distx;
		uint32_t disty = (y >> 4) - (pos.y >> 8); disty *= disty;
		const uint32_t dist = sqrt(distx + disty);
		const uint32_t clrdist = ColourDistance(lastclr, clr);
		
			
		
		lastclr = clr;
		wrld->put_px(x, y, clr, rank);
		updated();
	
}

void Client::teleport(const int32_t x, const int32_t y) {
	uint8_t msg[9] = {TELEPORT};
	memcpy(&msg[1], (char *)&x, sizeof(x));
	memcpy(&msg[5], (char *)&y, sizeof(y));
	ws.send((const char *)&msg, sizeof(msg), uWS::BINARY);
	pos.x = (x << 4) + 8;
	pos.y = (y << 4) + 8;
	wrld->upd_cli(this);
}

void Client::move(const pinfo_t& newpos) {
	pos = newpos;
	wrld->upd_cli(this);
	updated();
}

const pinfo_t * Client::get_pos() { /* Hmmm... */
	return &pos;
}

bool Client::can_chat() {
	return is_admin() || chatlimit.can_spend();
}

void Client::chat(const std::string& msg) {
	if (!mute) {
		if(chathtml) {
			wrld->broadcast("(A) " + get_nick() + ": " + msg);
		} else {
			wrld->broadcast("" + get_nick() + ": " + msg);
		}
	}
}

void Client::tell(const std::string& msg) {
	ws.send(msg.c_str(), msg.size(), uWS::TEXT);
}

void Client::updated() {
	uv_timer_again(&idletimeout_hdl);
}

void Client::idle_timeout(uv_timer_t * const t) {
	//Client * const cl = (Client *)t->data;
	/* Maybe there should be a proper kick function */
	//cl->tell("Server: Kicked for inactivity.");
	//cl->safedelete(true);
}

void Client::safedelete(const bool close) {
	if(handledelete){
		handledelete = false;
		wrld->rm_cli(this);
		uv_timer_stop(&idletimeout_hdl);
		uv_close((uv_handle_t *)&idletimeout_hdl, (uv_close_cb)([](uv_handle_t * const t){
			delete (Client *)t->data;
		}));
		if(close){
			ws.close();
		}
	}
}

void Client::promote(uint8_t newrank, uint16_t prate) {		
	rank = newrank;
	if (rank == ADMIN) {
		tell("Server: You are now an admin. Do /help for a list of commands.");
	} else if (rank == MODERATOR) {
		tell("Server: You are now a moderator.");
		set_pbucket(prate, 2);
	} else if (rank == USER) {
		set_pbucket(prate, 4);
	} else {
		set_pbucket(0, 1);
	}
	uint8_t msg[2] = {PERMISSIONS, rank};
	ws.send((const char *)&msg, sizeof(msg), uWS::BINARY);
}

void Client::enableHtmlChat() {
	chathtml = true;
	//tell("Server: Promoted to HTML chat");
}

bool Client::warn() {
	if(!is_admin() && ++penalty > CLIENT_MAX_WARN_LEVEL){
		safedelete(true);
		return true;
	}
	return false;
}

bool Client::is_mod() const {
	return rank == MODERATOR;
}

bool Client::is_admin() const {
	return rank == ADMIN;
}

uWS::WebSocket<uWS::SERVER> Client::get_ws() const {
	return ws;
}

std::string Client::get_nick() const {
	if (nick.size()) {
		return nick;
	}
	std::string e(is_mod() && !stealthadmin ? " " : is_admin() && !stealthadmin ? " " : "");
	e += std::to_string(id);
	return e;
}

World * Client::get_world() const {
	return wrld;
}

uint16_t Client::get_penalty() const {
	return penalty;
}

uint8_t Client::get_rank() const {
	return rank;
}

void Client::set_stealth(bool new_state) {
	stealthadmin = new_state;
}

void Client::set_nick(const std::string & name) {
	nick = name;
}

void Client::set_pbucket(uint16_t rate, uint16_t per) {
	pixupdlimit.set(rate, per);
	uint8_t msg[5] = {SET_PQUOTA};
	memcpy(&msg[1], (char *)&rate, sizeof(rate));
	memcpy(&msg[3], (char *)&per, sizeof(per));
	ws.send((const char *)&msg, sizeof(msg), uWS::BINARY);
}
