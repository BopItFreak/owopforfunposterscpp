#pragma once

#include <uWS.h>
#include <utility>
#include <iostream>
#include <unordered_map>
#include <map>
#include <utility>
#include <unordered_set>
#include <cstdio>
#include <set>
#include <fstream>
#include <memory>
#include <atomic>
#include <mutex>
#include "limiter.hpp"
#include "config.hpp"

#include "AsyncHTTPGETClient.hpp"
#include "TaskBuffer.hpp"

class Client;
class Chunk;
class World;
class Server;

size_t getUTF8strlen(const std::string& str);

inline std::string key(int32_t i, int32_t j) {
	return std::string((char *)&i, sizeof(i)) + std::string((char *)&j, sizeof(j));
};

enum server_messages : uint8_t {
	SET_ID,
	UPDATE,
	CHUNKDATA,
	TELEPORT,
	PERMISSIONS,
	CAPTCHA_REQUIRED,
	SET_PQUOTA,
	CHUNK_PROTECTED
};

struct pinfo_t {
	int32_t x;
	int32_t y;
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t tool;
};

struct pixupd_t {
	int32_t x;
	int32_t y;
	uint8_t r;
	uint8_t g;
	uint8_t b;
} __attribute__((packed));

struct chunkpos_t {
	int32_t x;
	int32_t y;
};

struct RGB {
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

enum captcha_verify_state : uint8_t {
	CA_WAITING,
	CA_VERIFYING,
	CA_VERIFIED,
	CA_OK,
	CA_INVALID
};

struct SocketInfo {
	std::string origin;
	std::string ip;
	Client * player;
	std::atomic<uint8_t> captcha_verified;
};

double ColourDistance(RGB e1, RGB e2);

class Database {
	const std::string dir;
	bool created_dir;
	std::unordered_map<std::string, std::fstream *> handles;
	std::set<std::string> nonexistant;
	std::map<std::string, std::string> worldProps;
	std::unordered_set<uint64_t> rankedChunks;
	bool changedPropsOrProtects;

public:
	Database(const std::string& dir);
	~Database();
	
	void save();
	
	void setChunkProtection(int32_t x, int32_t y, bool state);
	bool getChunkProtection(int32_t x, int32_t y);
	
	std::string getProp(std::string key, std::string defval = "");
	void setProp(std::string key, std::string value);

	std::fstream * get_handle(const int32_t x, const int32_t y, const bool create);

	bool get_chunk(const int32_t x, const int32_t y, char * const arr);
	void set_chunk(const int32_t x, const int32_t y, const char * const arr);
};

class Chunk {
	Database * const db;
	const uint32_t bgclr;
	const int32_t cx;
	const int32_t cy;
	uint8_t data[16 * 16 * 3];
	bool changed;

public:
	bool ranked;
	
	Chunk(const int32_t cx, const int32_t cy, const uint32_t bgclr, Database * const);
	~Chunk();

	size_t compress_data_to(uint8_t (&msg)[16 * 16 * 3 + 10 + 4]);
	uWS::WebSocket<uWS::SERVER>::PreparedMessage * get_prepd_data_msg();
	
	bool set_data(const uint8_t x, const uint8_t y, const RGB);
	void send_data(uWS::WebSocket<uWS::SERVER>, bool compressed = false);
	void save();

	void clear();
	uint8_t * get_data();
	void set_data(char const * const, size_t);
};

class Client {
public:
	enum Rank : uint8_t {
		NONE = 0,
		USER = 1,
		MODERATOR = 2,
		ADMIN = 3
	};
private:
	std::string nick;
	limiter::Bucket pixupdlimit;
	limiter::Bucket chatlimit;
	uv_timer_t idletimeout_hdl;
	uWS::WebSocket<uWS::SERVER> ws;
	World * const wrld;
	uint16_t penalty;
	bool handledelete;
	uint8_t rank;
	bool stealthadmin;
	bool suspicious;
	bool compressionEnabled;
	pinfo_t pos;
	RGB lastclr;
	bool chathtml;

public:
	const uint32_t id;
	SocketInfo * si;
	bool mute;

	Client(const uint32_t id, uWS::WebSocket<uWS::SERVER>, World * const, SocketInfo * si);
	~Client();

	bool can_edit();

	void get_chunk(const int32_t x, const int32_t y) const;
	void put_px(const int32_t x, const int32_t y, const RGB);

	void teleport(const int32_t x, const int32_t y);
	void move(const pinfo_t&);
	const pinfo_t * get_pos();

	bool can_chat();
	void chat(const std::string&);
	void tell(const std::string&);

	void updated();
	static void idle_timeout(uv_timer_t * const);

	void safedelete(const bool close);

	void promote(uint8_t, uint16_t);

	void enableHtmlChat();

	bool warn();

	bool is_mod() const;
	bool is_admin() const;
	uWS::WebSocket<uWS::SERVER> get_ws() const;
	std::string get_nick() const;
	World * get_world() const;
	uint16_t get_penalty() const;
	uint8_t get_rank() const;
	
	void set_stealth(bool);
	void set_nick(const std::string&);
	void set_pbucket(uint16_t rate, uint16_t per);
};

class World {
	uint32_t bgclr;
	uint32_t pids;
	uint16_t paintrate;
	uint8_t defaultRank;
	uv_timer_t upd_hdl;
	Database db;
	std::string pass;
	std::set<Client *> clients;
	std::unordered_map<std::string, Chunk *> chunks;
	std::vector<pixupd_t> pxupdates;
	std::set<Client *> plupdates;
	std::set<uint32_t> plleft;

public:
	const std::string name;

	World(const std::string& path, const std::string& name);
	~World();
	
	void update_all_clients();
	
	void setChunkProtection(int32_t x, int32_t y, bool state);
	
	void reload();
	std::string getProp(std::string key, std::string defval = "");
	void setProp(std::string key, std::string value);

	uint32_t get_id();
	void add_cli(Client * const);
	void upd_cli(Client * const);
	void rm_cli(Client * const);
	Client * get_cli(const uint32_t id) const;
	Client * get_cli(const std::string name) const;
	
	std::set<Client *> * get_pl();

	void sched_updates();
	static void send_updates(uv_timer_t * const);

	Chunk * get_chunk(const int32_t x, const int32_t y, bool create = true);
	void send_chunk(uWS::WebSocket<uWS::SERVER>, const int32_t x, const int32_t y, bool compressed = false);
	void del_chunk(const int32_t x, const int32_t y);
	void paste_chunk(const int32_t x, const int32_t y, char const * const);
	bool put_px(const int32_t x, const int32_t y, const RGB, uint8_t placerRank);

	void safedelete();

	void broadcast(const std::string& msg) const;

	void save();

	bool is_empty() const;
	bool is_pass(std::string const&) const;
	void set_default_rank(uint8_t);
	uint8_t get_default_rank();
	uint16_t get_paintrate();
};


class Commands {
	std::unordered_map<std::string, std::function<void(Client * const, const std::vector<std::string>&)>> usrcmds;
	std::unordered_map<std::string, std::function<void(Client * const, const std::vector<std::string>&)>> modcmds;
	std::unordered_map<std::string, std::function<void(Client * const, const std::vector<std::string>&)>> admincmds;

public:
	Commands(Server * const);

	std::string get_cmd_list(uint8_t rank) const;

	static std::vector<std::string> split_args(const std::string&);

	bool exec(Client * const, const std::string& msg) const;

  static void dev(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void pass(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void modlogin(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void adminlogin(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	
	static void getprop(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void setprop(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void reload(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	
	static void teleport(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void stealth(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void setrank(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void restrict(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);

	static void doas(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void kickip(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void save(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void setpbucket(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void banip(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void help(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void getid(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void kick(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void kickall(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void lock(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void lockdown(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void whois(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void bans(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void bansuspicious(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void whitelist(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void blacklist(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void ids(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void nick(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void tell(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void mute(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void worlds(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void totalonline(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void sayraw(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void tellraw(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
	static void broadcast(Server * const, const Commands * const, Client * const, const std::vector<std::string>& args);
};

class Server {
public:
	const uint16_t port;
	const std::string modpw;
	const std::string adminpw;
  const std::string devpw;
	const std::string path;
	const Commands cmds;
	uv_timer_t save_hdl;
	std::unordered_map<std::string, World *> worlds;
	std::unordered_set<uWS::WebSocket<uWS::SERVER>> connsws;
	std::unordered_set<std::string> ipwhitelist;
	std::unordered_set<std::string> ipblacklist;
	std::unordered_set<std::string> ipban;
	std::unordered_map<std::string, uint8_t> conns;
	limiter::Bucket connlimiter;
	uWS::Hub h;
	AsyncHTTPGETClient hcli;
	TaskBuffer async_tasks;
	uint32_t maxconns;
	bool captcha_required;
	bool lockdown;
	bool proxy_lock;
	bool instaban;
	bool trusting_captcha;
	uint8_t fastconnectaction;
	
	std::unordered_set<std::string> proxyquery_checking;
	
	Server(const uint16_t port, const std::string& modpw, const std::string& adminpw, const std::string& devpw, const std::string& path);
	~Server();

	void broadcastmsg(const std::string&);

	void run();
	void quit();

	void save_now();
	static void save_chunks(uv_timer_t * const);

	void join_world(uWS::WebSocket<uWS::SERVER>, const std::string&);

	bool is_adminpw(const std::string&);
	bool is_modpw(const std::string&);
  bool is_devpw(const std::string&);
	uint32_t get_conns(const std::string&);
	
	void admintell(const std::string&);
	
	void kickall(World * const);
	void kickall();
	void kickip(const std::string&);
	
	void banip(const std::string&);
	std::unordered_set<std::string> * getbans();
	std::unordered_set<std::string> * getwhitelist();
	std::unordered_set<std::string> * getblacklist();
	void whitelistip(const std::string&);
	
	void set_max_ip_conns(uint8_t);
	void set_captcha_protection(bool);
	void lockdown_check();
	void set_lockdown(bool);
	void set_instaban(bool);
	void set_proxycheck(bool);
	
	bool is_connected(const uWS::WebSocket<uWS::SERVER>);
	
	void writefiles();
	void readfiles();
};
