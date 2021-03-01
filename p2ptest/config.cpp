#include "config.h"
#include "log.h"


void print_help()
{
	printf("<short>   <command>   <argtype>             <info>");
	printf("-h        --help      [void]                Help");
	printf("-m        --master    [void]                Launch as master node");
	printf("-a        --address   [string:ipv4addr]     Set master node address (not used with '--master')");
	printf("-e        --endpoint  [string:ipv4addr]     Set local socket address ('0.0.0.0:48800' by default)");
	printf("          --localport [int]                 Set port for local socket ('48800' by default)");
}

void read_string(int argc, char const* argv[], int& i, std::string& outStr)
{
	if (i + 1 == argc) {
		printf("Invalid command line format: expect string after '%s'", argv[i]);
		return;
	}

	i += 1;
	outStr = argv[i];
}

void read_address(int argc, char const* argv[], int& i, NetAddress& outAddr)
{
	if (i + 1 > argc) {
        log(0, "Invalid command line format: expect string+int after '%s'", argv[i]);
		return;
	}

	i += 1;
	if (resolve_url(true, argv[i], outAddr) != 0) {
        log(0, "Invalid command line format: ipv4 address '%s' after '%s' is incorrect", argv[i], argv[i - 1]);
		return;
	}
}


void read_port(int argc, char const* argv[], int& i, NetAddress& outAddr)
{
	if (i + 1 == argc) {
		log(0, "Invalid command line format: expect port number (integer) after '%s'", argv[i]);
		return;
	}

	i += 1;
	outAddr.setport(strtol(argv[i], NULL, 10));
}


Config::Config(int argc, char const* argv[])
	: Config()
{
	for (int i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			mode = Mode::Help;
			print_help();
			return;
		}
		else if (!strcmp(argv[i], "-m") || !strcmp(argv[i], "--master")) {
			mode = Mode::Master;
		}
        else if (!strcmp(argv[i], "-n") || !strcmp(argv[i], "--nickname")) {
			read_string(argc, argv, i, nickname);
		}
        else if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "--remote-address")) {
			read_address(argc, argv, i, remoteServerAddress);
		}
        else if (!strcmp(argv[i], "-l") || !strcmp(argv[i], "--local-address")) {
			read_address(argc, argv, i, localServerAddress);
		}
        else if (!strcmp(argv[i], "-e") || !strcmp(argv[i], "--endpoint")) {
			read_address(argc, argv, i, endpoint);
		}
        else if (!strcmp(argv[i], "--localport")) {
			read_port(argc, argv, i, endpoint);
		}
	}
}


bool Config::isValid() const
{
	if (mode == Config::Mode::Unknown) return false;
	if (nickname.empty()) return false;

	if (!isMaster()) {
		if (remoteServerAddress.getport() == 0) return false;
	}
	return true;
}