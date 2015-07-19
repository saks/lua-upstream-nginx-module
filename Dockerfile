FROM saksmlz/lua-upstream-nginx-module-test:latest

ADD . /ngx_lua_upstream/

RUN bash /entrypoint
