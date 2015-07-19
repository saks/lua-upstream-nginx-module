# vim:set ft= ts=4 sw=4 et fdm=marker:

use Test::Nginx::Socket::Lua;

#worker_connections(1014);
#master_on();
#workers(2);
#log_level('warn');

repeat_each(1);

plan tests => repeat_each() * (blocks() * 3);

$ENV{TEST_NGINX_MEMCACHED_PORT} ||= 11211;

$ENV{TEST_NGINX_MY_INIT_CONFIG} = <<_EOC_;
lua_package_path "t/lib/?.lua;;";
_EOC_

#no_diff();
no_long_string();
run_tests();

__DATA__

=== TEST 1: should return error for bad upstream name
--- http_config
--- config
    location /t {
        content_by_lua '
            local upstream = require "ngx.upstream"
            local ok, error = upstream.add_peer("foo", { url = "127.0.0.2" })
            ngx.say(ok)
            ngx.say(error)
        ';
    }
--- request
    GET /t
--- response_body
false
upstream not found
--- no_error_log
[error]

=== TEST 2: should return false for existing peer
--- http_config
    upstream foo {
        server 127.0.0.2;
    }
--- config
    location /t {
        content_by_lua '
            local upstream = require "ngx.upstream"
            local ok, error = upstream.add_peer("foo", { url = "127.0.0.2" })
            ngx.say(ok)
            ngx.say(error)
        ';
    }
--- request
    GET /t
--- response_body
false
server already exists
--- no_error_log
[error]

=== TEST 3: should return false for existing multi-peer server
--- http_config
    upstream foo {
        server multi-ip-test.openresty.com;
    }
--- config
    location /t {
        content_by_lua '
            local upstream = require "ngx.upstream"
            local ok, error = upstream.add_peer("foo", { url = "multi-ip-test.openresty.com" })
            ngx.say(ok)
            ngx.say(error)
        ';
    }
--- request
    GET /t
--- response_body
false
server already exists
--- no_error_log
[error]

=== TEST 4: should return false for existing peer with default port
--- http_config
    upstream foo {
        server 127.0.0.2;
    }
--- config
    location /t {
        content_by_lua '
            local upstream = require "ngx.upstream"
            local ok, error = upstream.add_peer("foo", { url = "127.0.0.2:80" })
            ngx.say(ok)
            ngx.say(error)
        ';
    }
--- request
    GET /t
--- response_body
false
server already exists
--- no_error_log
[error]

=== TEST 5: should return false for existing peer with the same port
--- http_config
    upstream foo {
        server 127.0.0.2:80;
    }
--- config
    location /t {
        content_by_lua '
            local upstream = require "ngx.upstream"
            local ok, error = upstream.add_peer("foo", { url = "127.0.0.2:80" })
            ngx.say(ok)
            ngx.say(error)
        ';
    }
--- request
    GET /t
--- response_body
false
server already exists
--- no_error_log
[error]

=== TEST 6: should return true for existing peer with different port
--- http_config
    upstream foo {
        server 127.0.0.2:80;
    }
--- config
    location /t {
        content_by_lua '
            local upstream = require "ngx.upstream"
            local ok, error = upstream.add_peer("foo", { url = "127.0.0.2:81" })
            ngx.say(ok)
            ngx.say(error)
        ';
    }
--- request
    GET /t
--- response_body
true
nil
--- no_error_log
[error]

=== TEST 7: should add peer with similar ip
--- http_config
    upstream foo {
        server 127.0.0.123;
    }
--- config
    location /t {
        content_by_lua '
            local upstream = require "ngx.upstream"
            local ok, error = upstream.add_peer("foo", { url = "127.0.0.12" })
            ngx.say(ok)
            ngx.say(error)
        ';
    }
--- request
    GET /t
--- response_body
true
nil
--- no_error_log
[error]

=== TEST 8: should add peer with weight from lua
--- http_config
    upstream foo {
        server 127.0.0.1 weight=122;
    }
--- config
    location /t {
        content_by_lua '
            local upstream = require "ngx.upstream"

            upstream.add_peer("foo", { url = "127.0.0.2" })
            upstream.add_peer("foo", { url = "127.0.0.3", weight = 123 })
            upstream.add_peer("foo", { url = "127.0.0.4", weight = "124" })
            upstream.add_peer("foo", { url = "127.0.0.6", weight = nil })
            upstream.add_peer("foo", { url = "127.0.0.5", weight = "" })
            upstream.add_peer("foo", { url = "127.0.0.7", weight = 0 })
            upstream.add_peer("foo", { url = "127.0.0.8", weight = "0" })

            local peers, err = upstream.get_primary_peers("foo")
            for _, peer in pairs(peers) do
                ngx.say(peer.weight)
            end
        ';
    }
--- request
    GET /t
--- response_body
122
1
123
124
1
1
0
0
--- no_error_log
[error]

=== TEST 9: should add backup peer
--- http_config
    upstream foo {
        server 127.0.0.1;
    }
--- config
    location /t {
        content_by_lua '
            local upstream = require "ngx.upstream"

            -- TODO: figure out why I cannot test backup field
            -- upstream.add_peer("foo", { url = "127.0.0.2:8000", backup = 1 })
            -- upstream.add_peer("foo", { url = "127.0.0.2:8001", backup = "true" })
            -- upstream.add_peer("foo", { url = "127.0.0.2:8002", backup = 1 })
            -- upstream.add_peer("foo", { url = "127.0.0.2:8003", backup = "1" })
            --
            -- upstream.add_peer("foo", { url = "127.0.0.3:9000", backup = false })
            -- upstream.add_peer("foo", { url = "127.0.0.3:9001", backup = "false" })
            -- upstream.add_peer("foo", { url = "127.0.0.3:9002", backup = "" })
            -- upstream.add_peer("foo", { url = "127.0.0.3:9003", backup = nil })
            -- upstream.add_peer("foo", { url = "127.0.0.3:9004", backup = 0 })
            -- upstream.add_peer("foo", { url = "127.0.0.3:9005", backup = "0" })

            ngx.say("primary:")
            local peers, err = upstream.get_primary_peers("foo")
            for _, peer in pairs(peers) do
                ngx.say(peer.name)
            end
            ngx.say("backup:")
            local peers, err = upstream.get_backup_peers("foo")
            for _, peer in pairs(peers) do
                ngx.say(peer.name)
            end
        ';
    }
--- request
    GET /t
--- response_body
primary:
127.0.0.1:80
backup:
--- no_error_log
[error]

=== TEST 10: should add backup peer
--- http_config
    upstream foo {
        server 127.0.0.1;
    }
--- config
    location /t {
        content_by_lua '
            local upstream = require "ngx.upstream"

            upstream.add_peer("foo", { url = "127.0.0.2:8000", down = 1 })
            upstream.add_peer("foo", { url = "127.0.0.2:8001", down = "true" })
            upstream.add_peer("foo", { url = "127.0.0.2:8002", down = 1 })
            upstream.add_peer("foo", { url = "127.0.0.2:8003", down = "1" })

            upstream.add_peer("foo", { url = "127.0.0.3:9000", down = false })
            upstream.add_peer("foo", { url = "127.0.0.3:9001", down = "false" })
            upstream.add_peer("foo", { url = "127.0.0.3:9002", down = "" })
            upstream.add_peer("foo", { url = "127.0.0.3:9003", down = nil })
            upstream.add_peer("foo", { url = "127.0.0.3:9004", down = 0 })
            upstream.add_peer("foo", { url = "127.0.0.3:9005", down = "0" })

            local peers, err = upstream.get_primary_peers("foo")
            for _, peer in pairs(peers) do
                ngx.print(peer.name.." is ")
                if peer.down then
                    ngx.say("down")
                else
                    ngx.say("up")
                end
            end
        ';
    }
--- request
    GET /t
--- response_body
127.0.0.1:80 is up
127.0.0.2:8000 is down
127.0.0.2:8001 is down
127.0.0.2:8002 is down
127.0.0.2:8003 is down
127.0.0.3:9000 is up
127.0.0.3:9001 is up
127.0.0.3:9002 is up
127.0.0.3:9003 is up
127.0.0.3:9004 is up
127.0.0.3:9005 is up
--- no_error_log
[error]

=== TEST 11: should add peer with max_fails
--- http_config
    upstream foo {
        server 127.0.0.1;
    }
--- config
    location /t {
        content_by_lua '
            local upstream = require "ngx.upstream"

            upstream.add_peer("foo", { url = "127.0.0.2:8000", max_fails = 0 })
            upstream.add_peer("foo", { url = "127.0.0.2:8001", max_fails = 1 })
            upstream.add_peer("foo", { url = "127.0.0.2:8002", max_fails = 2 })
            upstream.add_peer("foo", { url = "127.0.0.2:8003", max_fails = "0" })
            upstream.add_peer("foo", { url = "127.0.0.2:8004", max_fails = "1" })
            upstream.add_peer("foo", { url = "127.0.0.2:8005", max_fails = "2" })
            upstream.add_peer("foo", { url = "127.0.0.2:8006", max_fails = "" })
            upstream.add_peer("foo", { url = "127.0.0.2:8007", max_fails = nil })

            local peers, err = upstream.get_primary_peers("foo")
            for _, peer in pairs(peers) do
                ngx.say(peer.name.." "..peer.max_fails)
            end
        ';
    }
--- request
    GET /t
--- response_body
127.0.0.1:80 1
127.0.0.2:8000 0
127.0.0.2:8001 1
127.0.0.2:8002 2
127.0.0.2:8003 0
127.0.0.2:8004 1
127.0.0.2:8005 2
127.0.0.2:8006 1
127.0.0.2:8007 1
--- no_error_log
[error]

=== TEST 12: should add peer with fail_timeout
--- http_config
    upstream foo {
        server 127.0.0.1;
    }
--- config
    location /t {
        content_by_lua '
            local upstream = require "ngx.upstream"

            upstream.add_peer("foo", { url = "127.0.0.2:8000", fail_timeout = 0 })
            upstream.add_peer("foo", { url = "127.0.0.2:8001", fail_timeout = 1 })
            upstream.add_peer("foo", { url = "127.0.0.2:8002", fail_timeout = 2 })
            upstream.add_peer("foo", { url = "127.0.0.2:8003", fail_timeout = "0" })
            upstream.add_peer("foo", { url = "127.0.0.2:8004", fail_timeout = "1" })
            upstream.add_peer("foo", { url = "127.0.0.2:8005", fail_timeout = "2" })
            upstream.add_peer("foo", { url = "127.0.0.2:8006", fail_timeout = "" })
            upstream.add_peer("foo", { url = "127.0.0.2:8007", fail_timeout = nil })

            local peers, err = upstream.get_primary_peers("foo")
            for _, peer in pairs(peers) do
                ngx.say(peer.name.." "..peer.fail_timeout)
            end
        ';
    }
--- request
    GET /t
--- response_body
127.0.0.1:80 10
127.0.0.2:8000 0
127.0.0.2:8001 1
127.0.0.2:8002 2
127.0.0.2:8003 0
127.0.0.2:8004 1
127.0.0.2:8005 2
127.0.0.2:8006 10
127.0.0.2:8007 10
--- no_error_log
[error]


=== TEST 13: get dynamically added peers
--- http_config
    $TEST_NGINX_MY_INIT_CONFIG
    upstream foo {
        zone upstream_dynamic 64k;
        server 127.0.0.1;
    }
--- config
    location /t {
        content_by_lua '
            local upstream = require "ngx.upstream"
            local new_peer = {
                url = "127.0.0.1:8081",
                weight = 123,
                max_fails = 11,
                fail_timeout = 30,
                down = true,
                backup = false,
            }

            upstream.add_peer("foo", new_peer)

            local ljson = require "ljson"
            local peers, err = upstream.get_primary_peers("foo")
            if not peers then
                ngx.say("failed to get peers: ", err)
                return
            end
            ngx.say(ljson.encode(peers))
        ';
    }
--- request
  GET /t
--- response_body
[{"current_weight":0,"effective_weight":1,"fail_timeout":10,"fails":0,"id":0,"max_fails":1,"name":"127.0.0.1:80","weight":1},{"current_weight":0,"down":true,"effective_weight":123,"fail_timeout":30,"fails":0,"id":1,"max_fails":11,"name":"127.0.0.1:8081","weight":123}]
--- no_error_log
[error]
