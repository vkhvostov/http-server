#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>

#include "network/Config.hpp"

int main() {
    Config config;

    config.parse("configs/default.conf");
    assert(config.getServers().size() == 1);
    assert(config.getServers()[0].port == 8080);
    assert(config.getServers()[0].routes.size() == 5);
    assert(config.getServers()[0].routes[0].path == "/");
    assert(config.getServers()[0].clientMaxBodySize == 1048576);
    std::cout << "Test 1 passed: default.conf" << std::endl;

    Config config2;
    config2.parse("configs/multi.conf");
    assert(config2.getServers().size() == 2);
    assert(config2.getServers()[0].port == 8080);
    assert(config2.getServers()[1].port == 8081);
    std::cout << "Test 2 passed: multi.conf" << std::endl;

    Config config3;
    config3.parse("configs/cgi.conf");
    bool found = false;
    for (size_t i = 0; i < config3.getServers()[0].routes.size(); ++i) {
        if (config3.getServers()[0].routes[i].cgiExtension == ".py") {
            found = true;
            assert(config3.getServers()[0].routes[i].cgiPath == "/usr/bin/python3");
        }
    }
    assert(found);
    std::cout << "Test 3 passed: cgi.conf" << std::endl;

    bool threw = false;
    try {
        Config bad;
        bad.parse("configs/nonexistent.conf");
    } catch (const std::exception &) {
        threw = true;
    }
    assert(threw);
    std::cout << "Test 4 passed: invalid file" << std::endl;

    {
        const char *tmpfile = "configs/http-server_test_comments.conf";
        std::ofstream out(tmpfile);
        out << "# full line comment\n";
        out << "server {\n";
        out << "  listen 8082; # inline comment\n";
        out << "  client_max_body_size 10K;\n";
        out << "  location / { root ./www; }\n";
        out << "}\n";
        out.close();
        Config comments;
        comments.parse(tmpfile);
        assert(comments.getServers().size() == 1);
        assert(comments.getServers()[0].port == 8082);
        assert(comments.getServers()[0].clientMaxBodySize == 10240);
        std::remove(tmpfile);
        std::cout << "Test 5 passed: comments + 10K size parsing" << std::endl;
    }

    {
        const char *tmpfile = "configs/http-server_test_size.conf";
        std::ofstream out(tmpfile);
        out << "server {\n";
        out << "  listen 8083;\n";
        out << "  client_max_body_size 1G;\n";
        out << "  location / { root ./www; }\n";
        out << "}\n";
        out.close();
        Config sizeCfg;
        sizeCfg.parse(tmpfile);
        assert(sizeCfg.getServers()[0].clientMaxBodySize == 1073741824UL);
        std::remove(tmpfile);
        std::cout << "Test 6 passed: 1G size parsing" << std::endl;
    }

    {
        const char *tmpfile = "configs/http-server_test_unknown.conf";
        std::ofstream out(tmpfile);
        out << "server {\n";
        out << "  listen 8084;\n";
        out << "  unknown_dir value;\n";
        out << "  location / { root ./www; }\n";
        out << "}\n";
        out.close();
        bool unknownThrew = false;
        try {
            Config badUnknown;
            badUnknown.parse(tmpfile);
        } catch (const std::exception &) {
            unknownThrew = true;
        }
        assert(unknownThrew);
        std::remove(tmpfile);
        std::cout << "Test 7 passed: unknown directive rejected" << std::endl;
    }

    {
        const char *tmpfile = "configs/http-server_test_missing_brace.conf";
        std::ofstream out(tmpfile);
        out << "server \n";
        out << "  listen 8085;\n";
        out << "}\n";
        out.close();
        bool braceThrew = false;
        try {
            Config badBrace;
            badBrace.parse(tmpfile);
        } catch (const std::exception &) {
            braceThrew = true;
        }
        assert(braceThrew);
        std::remove(tmpfile);
        std::cout << "Test 8 passed: missing brace rejected" << std::endl;
    }

    {
        const char *tmpfile = "configs/http-server_test_bad_port.conf";
        std::ofstream out(tmpfile);
        out << "server {\n";
        out << "  listen 70000;\n";
        out << "  location / { root ./www; }\n";
        out << "}\n";
        out.close();
        bool badPortThrew = false;
        try {
            Config badPort;
            badPort.parse(tmpfile);
        } catch (const std::exception &) {
            badPortThrew = true;
        }
        assert(badPortThrew);
        std::remove(tmpfile);
        std::cout << "Test 9 passed: invalid port rejected" << std::endl;
    }

    std::cout << "All Phase 1 tests passed!" << std::endl;
    return 0;
}
