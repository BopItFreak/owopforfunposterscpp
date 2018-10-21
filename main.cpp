#include "server.hpp"
#include <signal.h>

/* Just for the signal handler */
static Server * srvptr;

std::string gen_random_str(const size_t size) {
	srand(time(NULL));
	static const char alphanum[] =
		"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz?_";

	std::string str(size, '0');
	for(size_t i = 0; i < size; ++i){
		str[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
	}
	return str;
}

void handler(int s) {
	std::cout << "Saving worlds and exiting..." << std::endl;
	srvptr->quit();
}

int main(int argc, char * argv[]) {
	std::cout << "Starting server..." << std::endl;
	srvptr = new Server(argc > 1 ? std::stoul(argv[1]) : 1337,
						argc > 2 ? argv[2] : gen_random_str(10),
						argc > 3 ? argv[3] : gen_random_str(10),
            argc > 4 ? argv[4] : gen_random_str(10),
						argc > 5 ? argv[5] : "chunkdata");
	
	struct sigaction sigIntHandler;
	sigIntHandler.sa_handler = handler;
	sigemptyset(&sigIntHandler.sa_mask);
	sigIntHandler.sa_flags = 0;
	
	sigaction(SIGINT, &sigIntHandler, NULL);
	
	srvptr->run();
	delete srvptr;
	
	return 1;
}
