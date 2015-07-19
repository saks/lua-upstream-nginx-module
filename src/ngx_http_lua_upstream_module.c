
/*
 * Copyright (C) Yichun Zhang (agentzh)
 */


#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"


#include <ngx_core.h>
#include <ngx_http.h>
#include <lauxlib.h>
#include "ngx_http_lua_api.h"


ngx_module_t ngx_http_lua_upstream_module;


static ngx_int_t ngx_http_lua_upstream_init(ngx_conf_t *cf);
static int ngx_http_lua_upstream_create_module(lua_State * L);
static int ngx_http_lua_upstream_get_upstreams(lua_State * L);
static int ngx_http_lua_upstream_get_servers(lua_State * L);
static ngx_http_upstream_main_conf_t *
    ngx_http_lua_upstream_get_upstream_main_conf(lua_State *L);
static int ngx_http_lua_upstream_get_primary_peers(lua_State * L);
static int ngx_http_lua_upstream_get_backup_peers(lua_State * L);
static int ngx_http_lua_get_peer(lua_State *L,
    ngx_http_upstream_rr_peer_t *peer, ngx_uint_t id);
static ngx_http_upstream_srv_conf_t *
    ngx_http_lua_upstream_find_upstream(lua_State *L, ngx_str_t *host);
static ngx_http_upstream_rr_peer_t *
    ngx_http_lua_upstream_lookup_peer(lua_State *L);
static int ngx_http_lua_upstream_set_peer_down(lua_State * L);
static int ngx_http_lua_upstream_add_peer(lua_State * L);


static ngx_http_module_t ngx_http_lua_upstream_ctx = {
    NULL,                           /* preconfiguration */
    ngx_http_lua_upstream_init,     /* postconfiguration */
    NULL,                           /* create main configuration */
    NULL,                           /* init main configuration */
    NULL,                           /* create server configuration */
    NULL,                           /* merge server configuration */
    NULL,                           /* create location configuration */
    NULL                            /* merge location configuration */
};


ngx_module_t ngx_http_lua_upstream_module = {
    NGX_MODULE_V1,
    &ngx_http_lua_upstream_ctx,  /* module context */
    NULL,                        /* module directives */
    NGX_HTTP_MODULE,             /* module type */
    NULL,                        /* init master */
    NULL,                        /* init module */
    NULL,                        /* init process */
    NULL,                        /* init thread */
    NULL,                        /* exit thread */
    NULL,                        /* exit process */
    NULL,                        /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_http_lua_upstream_init(ngx_conf_t *cf)
{
    if (ngx_http_lua_add_package_preload(cf, "ngx.upstream",
                                         ngx_http_lua_upstream_create_module)
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static int
ngx_http_lua_upstream_create_module(lua_State * L)
{
    lua_createtable(L, 0, 1);

    lua_pushcfunction(L, ngx_http_lua_upstream_get_upstreams);
    lua_setfield(L, -2, "get_upstreams");

    lua_pushcfunction(L, ngx_http_lua_upstream_get_servers);
    lua_setfield(L, -2, "get_servers");

    lua_pushcfunction(L, ngx_http_lua_upstream_get_primary_peers);
    lua_setfield(L, -2, "get_primary_peers");

    lua_pushcfunction(L, ngx_http_lua_upstream_get_backup_peers);
    lua_setfield(L, -2, "get_backup_peers");

    lua_pushcfunction(L, ngx_http_lua_upstream_set_peer_down);
    lua_setfield(L, -2, "set_peer_down");

    lua_pushcfunction(L, ngx_http_lua_upstream_add_peer);
    lua_setfield(L, -2, "add_peer");

    return 1;
}


static int
ngx_http_lua_upstream_get_upstreams(lua_State * L)
{
    ngx_uint_t                            i;
    ngx_http_upstream_srv_conf_t        **uscfp, *uscf;
    ngx_http_upstream_main_conf_t        *umcf;

    if (lua_gettop(L) != 0) {
        return luaL_error(L, "no argument expected");
    }

    umcf = ngx_http_lua_upstream_get_upstream_main_conf(L);
    uscfp = umcf->upstreams.elts;

    lua_createtable(L, umcf->upstreams.nelts, 0);

    for (i = 0; i < umcf->upstreams.nelts; i++) {

        uscf = uscfp[i];

        lua_pushlstring(L, (char *) uscf->host.data, uscf->host.len);
        if (uscf->port) {
            lua_pushfstring(L, ":%d", (int) uscf->port);
            lua_concat(L, 2);

            /* XXX maybe we should also take "default_port" into account
             * here? */
        }

        lua_rawseti(L, -2, i + 1);
    }

    return 1;
}


static int
ngx_http_lua_upstream_get_servers(lua_State * L)
{
    ngx_str_t                             host;
    ngx_uint_t                            i, j, n;
    ngx_http_upstream_server_t           *server;
    ngx_http_upstream_srv_conf_t         *us;

    if (lua_gettop(L) != 1) {
        return luaL_error(L, "exactly one argument expected");
    }

    host.data = (u_char *) luaL_checklstring(L, 1, &host.len);

    us = ngx_http_lua_upstream_find_upstream(L, &host);
    if (us == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "upstream not found");
        return 2;
    }

    if (us->servers == NULL || us->servers->nelts == 0) {
        lua_newtable(L);
        return 1;
    }

    server = us->servers->elts;

    lua_createtable(L, us->servers->nelts, 0);

    for (i = 0; i < us->servers->nelts; i++) {

        n = 4;

        if (server[i].backup) {
            n++;
        }

        if (server[i].down) {
            n++;
        }

        lua_createtable(L, 0, n);

        lua_pushliteral(L, "addr");

        if (server[i].naddrs == 1) {
            lua_pushlstring(L, (char *) server[i].addrs->name.data,
                            server[i].addrs->name.len);

        } else {
            lua_createtable(L, server[i].naddrs, 0);

            for (j = 0; j < server[i].naddrs; j++) {
                lua_pushlstring(L, (char *) server[i].addrs[j].name.data,
                                server[i].addrs[j].name.len);
                lua_rawseti(L, -2, j + 1);
            }
        }

        lua_rawset(L, -3);

        lua_pushliteral(L, "weight");
        lua_pushinteger(L, (lua_Integer) server[i].weight);
        lua_rawset(L, -3);

        lua_pushliteral(L, "max_fails");
        lua_pushinteger(L, (lua_Integer) server[i].max_fails);
        lua_rawset(L, -3);

        lua_pushliteral(L, "fail_timeout");
        lua_pushinteger(L, (lua_Integer) server[i].fail_timeout);
        lua_rawset(L, -3);

        if (server[i].backup) {
            lua_pushliteral(L, "backup");
            lua_pushboolean(L, 1);
            lua_rawset(L, -3);
        }

        if (server[i].down) {
            lua_pushliteral(L, "down");
            lua_pushboolean(L, 1);
            lua_rawset(L, -3);
        }

        lua_rawseti(L, -2, i + 1);
    }

    return 1;
}


static int
ngx_http_lua_upstream_get_primary_peers(lua_State * L)
{
    ngx_str_t                             host;
    ngx_uint_t                            i;
    ngx_http_upstream_rr_peers_t         *peers;
    ngx_http_upstream_rr_peer_t          *peer;
    ngx_http_upstream_srv_conf_t         *us;

    if (lua_gettop(L) != 1) {
        return luaL_error(L, "exactly one argument expected");
    }

    host.data = (u_char *) luaL_checklstring(L, 1, &host.len);

    us = ngx_http_lua_upstream_find_upstream(L, &host);
    if (us == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "upstream not found");
        return 2;
    }

    peers = us->peer.data;

    if (peers == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "no peer data");
        return 2;
    }

    lua_createtable(L, peers->number, 0);

    for (peer = peers->peer, i = 0; peer; peer = peer->next, i++) {
        ngx_http_lua_get_peer(L, peer, i);
        lua_rawseti(L, -2, i + 1);
    }

    return 1;
}


static int
ngx_http_lua_upstream_get_backup_peers(lua_State * L)
{
    ngx_str_t                             host;
    ngx_uint_t                            i;
    ngx_http_upstream_rr_peers_t         *peers;
    ngx_http_upstream_rr_peer_t          *peer;
    ngx_http_upstream_srv_conf_t         *us;

    if (lua_gettop(L) != 1) {
        return luaL_error(L, "exactly one argument expected");
    }

    host.data = (u_char *) luaL_checklstring(L, 1, &host.len);

    us = ngx_http_lua_upstream_find_upstream(L, &host);
    if (us == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "upstream not found");
        return 2;
    }

    peers = us->peer.data;

    if (peers == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "no peer data");
        return 2;
    }

    peers = peers->next;
    if (peers == NULL) {
        lua_newtable(L);
        return 1;
    }

    lua_createtable(L, peers->number, 0);

    for (peer = peers->peer, i = 0; peer; peer = peer->next, i++) {
        ngx_http_lua_get_peer(L, peer, i);
        lua_rawseti(L, -2, i + 1);
    }

    return 1;
}


static int
ngx_http_lua_upstream_set_peer_down(lua_State * L)
{
    ngx_http_upstream_rr_peer_t          *peer;

    if (lua_gettop(L) != 4) {
        return luaL_error(L, "exactly 4 arguments expected");
    }

    peer = ngx_http_lua_upstream_lookup_peer(L);
    if (peer == NULL) {
        return 2;
    }

    peer->down = lua_toboolean(L, 4);

    lua_pushboolean(L, 1);
    return 1;
}

static int ngx_http_lua_upstream_add_peer(lua_State * L)
{
    ngx_http_upstream_rr_peer_t          *peer, *last_pp, **peerp;
    ngx_http_upstream_srv_conf_t         *uscf;
    ngx_http_upstream_rr_peers_t         *peers;
    ngx_http_request_t                   *r;
    ngx_str_t                             upstream_name, url_addr, host, str_opt;
    ngx_uint_t                            i, n, k;
    size_t                                peer_size;
    ngx_int_t                             weight, max_fails, down, backup;
    ngx_url_t                             u;
    time_t                                fail_timeout;
    int                                   vtype;

    if (lua_gettop(L) != 2) {
        return luaL_error(L, "exactly two arguments expected");
    }

    if (!lua_istable(L, -1)) {
        return luaL_argerror(L, -1, "second argument should be a table");
    }

    upstream_name.data = (u_char *) luaL_checklstring(L, 1, &upstream_name.len);
    uscf = ngx_http_lua_upstream_find_upstream(L, &upstream_name);

    if (uscf == NULL) {
      lua_pushboolean(L, 0);
      lua_pushliteral(L, "upstream not found");
      return 2;
    }

    weight = 1;
    max_fails = 1;
    fail_timeout = 10;
    backup = 0;
    down = 0;

    if (lua_gettop(L) != 2) {
        return luaL_error(L, "exactly two arguments expected");
    }

    r = ngx_http_lua_get_request(L);

    /* url */
    lua_getfield(L, -1, "url");
    host.data = (u_char *) luaL_checklstring(L, -1, &host.len);
    lua_pop(L, 1);

    /* weight */
    lua_getfield(L, -1, "weight");
    vtype = lua_type(L, -1);
    if (LUA_TNUMBER == vtype ||
        (LUA_TSTRING == vtype && ngx_strlen(luaL_checkstring(L, -1)))) {

        weight = (ngx_int_t) luaL_checkint(L, -1);
    }
    lua_pop(L, 1);

    /* backup */
    lua_getfield(L, -1, "backup");
    switch (lua_type(L, -1)) {
    case LUA_TNIL:
        /* do nothing */
        break;

    case LUA_TNUMBER:
        backup = (ngx_int_t) luaL_checkint(L, -1);
        break;
    case LUA_TBOOLEAN:
        backup = (ngx_int_t) lua_toboolean(L, -1);
        break;

    case LUA_TSTRING:
        str_opt.data = (u_char *) luaL_checklstring(L, -1, &str_opt.len);

        if (ngx_strlen(str_opt.data)) {
            backup = (ngx_strncmp(str_opt.data, (u_char *) "true", 4) == 0) ||
              (ngx_strncmp(str_opt.data, (u_char *) "1", 1) == 0);
        }
        break;

    default:
        return luaL_error(L, "Bad backup option value");
    }
    lua_pop(L, 1);

    /* down */
    lua_getfield(L, -1, "down");
    switch (lua_type(L, -1)) {
    case LUA_TNIL:
        /* do nothing */
        break;

    case LUA_TNUMBER:
        down = (ngx_int_t) luaL_checkint(L, -1);
        break;
    case LUA_TBOOLEAN:
        down = (ngx_int_t) lua_toboolean(L, -1);
        break;

    case LUA_TSTRING:
        str_opt.data = (u_char *) luaL_checklstring(L, -1, &str_opt.len);

        if (ngx_strlen(str_opt.data)) {
            down = (ngx_strncmp(str_opt.data, (u_char *) "true", 4) == 0) ||
              (ngx_strncmp(str_opt.data, (u_char *) "1", 1) == 0);
        }
        break;

    default:
        return luaL_error(L, "Bad down option value");
    }
    lua_pop(L, 1);
    /* ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0, "backup opt: %d", backup); */

    /* max_fails */
    lua_getfield(L, -1, "max_fails");
    switch (lua_type(L, -1)) {
    case LUA_TNIL:
        /* do nothing */
        break;

    case LUA_TNUMBER:
        max_fails = (ngx_int_t) luaL_checkint(L, -1);
        break;

    case LUA_TSTRING:
        str_opt.data = (u_char *) luaL_checklstring(L, -1, &str_opt.len);

        if (ngx_strlen(str_opt.data)) {
            max_fails = (ngx_int_t) lua_tointeger(L, -1);
        }
        break;

    default:
        return luaL_error(L, "Bad max_fails option value");
    }
    lua_pop(L, 1);

    /* fail_timeout */
    lua_getfield(L, -1, "fail_timeout");
    switch (lua_type(L, -1)) {
    case LUA_TNIL:
        /* do nothing */
        break;

    case LUA_TNUMBER:
        fail_timeout = (time_t) luaL_checkint(L, -1);
        break;

    case LUA_TSTRING:
        str_opt.data = (u_char *) luaL_checklstring(L, -1, &str_opt.len);

        if (ngx_strlen(str_opt.data)) {
            fail_timeout = (time_t) lua_tointeger(L, -1);
        }
        break;

    default:
        return luaL_error(L, "Bad fail_timeout option value");
    }
    lua_pop(L, 1);

    /* parse url */
    ngx_memzero(&u, sizeof(ngx_url_t));
    u.url = host;
    u.default_port = 80;

    if (ngx_parse_url(uscf->servers->pool, &u) != NGX_OK) {
        if (u.err) {
            ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0,
                "%s in upstream \"%V\"", u.err, &u.url);
        }
        return luaL_error(L, "Cannot parse url \"%V\"", &u.url);
    }


    peers = uscf->peer.data;

    if (backup) {
      peers = peers->next;
    }

    for (peer = peers->peer; peer; peer = peer->next) {
        for (k = 0; k < u.naddrs; k++) {
            url_addr = u.addrs[k].name;
            if (ngx_strncmp(peer->name.data, url_addr.data,
                  ngx_max(url_addr.len, peer->name.len)) == 0) {
                lua_pushboolean(L, 0);
                lua_pushliteral(L, "server already exists");
                return 2;
            }
        }
    }


    for (last_pp = peers->peer; last_pp->next; last_pp = last_pp->next);
    peerp = &last_pp->next;

    peer_size = sizeof(ngx_http_upstream_rr_peer_t) * u.naddrs;

#if (NGX_HTTP_UPSTREAM_ZONE)
    if (peers->shpool) {
        peer = ngx_slab_calloc_locked(peers->shpool, peer_size);
    } else {
        peer = ngx_pcalloc(uscf->servers->pool, peer_size);
    }
#else
    peer = ngx_pcalloc(uscf->servers->pool, peer_size);
#endif
    if (peer == NULL) {
        lua_pushboolean(L, 0);
        lua_pushliteral(L, "cannot allocate memory for a new peer");
        return 2;
    }

    n = 0;
    for (i = 0; i < u.naddrs; i++) {
        peer[n].sockaddr = u.addrs[i].sockaddr;
        peer[n].socklen = u.addrs[i].socklen;
        peer[n].name = u.addrs[i].name;
        peer[n].weight = weight;
        peer[n].effective_weight = weight;
        peer[n].current_weight = 0;
        peer[n].max_fails = max_fails;
        peer[n].fail_timeout = fail_timeout;
        peer[n].down = down;
        peer[n].server = u.url;

        *peerp = &peer[n];
        peerp = &peer[n].next;
        peers->number = peers->number + 1;
        n++;
    }

    lua_pushboolean(L, 1);
    return 1;
}

static ngx_http_upstream_rr_peer_t *
ngx_http_lua_upstream_lookup_peer(lua_State *L)
{
    int                                   id, i, backup;
    ngx_str_t                             host;
    ngx_http_upstream_srv_conf_t         *us;
    ngx_http_upstream_rr_peers_t         *peers;
    ngx_http_upstream_rr_peer_t          *peer;

    host.data = (u_char *) luaL_checklstring(L, 1, &host.len);

    us = ngx_http_lua_upstream_find_upstream(L, &host);
    if (us == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "upstream not found");
        return NULL;
    }

    peers = us->peer.data;

    if (peers == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "no peer data");
        return NULL;
    }

    backup = lua_toboolean(L, 2);
    if (backup) {
        peers = peers->next;
    }

    if (peers == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "no backup peers");
        return NULL;
    }

    id = luaL_checkint(L, 3);
    if (id < 0 || (ngx_uint_t) id >= peers->number) {
        lua_pushnil(L);
        lua_pushliteral(L, "bad peer id");
        return NULL;
    }


    for (peer = peers->peer, i = 0; i <= id; peer = peer->next, i++) {
        if (id == i) {
            return peer;
        }
    }

    lua_pushnil(L);
    lua_pushliteral(L, "failed to lookup a peer");
    return NULL;
}


static int
ngx_http_lua_get_peer(lua_State *L, ngx_http_upstream_rr_peer_t *peer,
    ngx_uint_t id)
{
    ngx_uint_t     n;

    n = 8;

    if (peer->down) {
        n++;
    }

    if (peer->accessed) {
        n++;
    }

    if (peer->checked) {
        n++;
    }

    lua_createtable(L, 0, n);

    lua_pushliteral(L, "id");
    lua_pushinteger(L, (lua_Integer) id);
    lua_rawset(L, -3);

    lua_pushliteral(L, "name");
    lua_pushlstring(L, (char *) peer->name.data, peer->name.len);
    lua_rawset(L, -3);

    lua_pushliteral(L, "weight");
    lua_pushinteger(L, (lua_Integer) peer->weight);
    lua_rawset(L, -3);

    lua_pushliteral(L, "current_weight");
    lua_pushinteger(L, (lua_Integer) peer->current_weight);
    lua_rawset(L, -3);

    lua_pushliteral(L, "effective_weight");
    lua_pushinteger(L, (lua_Integer) peer->effective_weight);
    lua_rawset(L, -3);

    lua_pushliteral(L, "fails");
    lua_pushinteger(L, (lua_Integer) peer->fails);
    lua_rawset(L, -3);

    lua_pushliteral(L, "max_fails");
    lua_pushinteger(L, (lua_Integer) peer->max_fails);
    lua_rawset(L, -3);

    lua_pushliteral(L, "fail_timeout");
    lua_pushinteger(L, (lua_Integer) peer->fail_timeout);
    lua_rawset(L, -3);

    if (peer->accessed) {
        lua_pushliteral(L, "accessed");
        lua_pushinteger(L, (lua_Integer) peer->accessed);
        lua_rawset(L, -3);
    }

    if (peer->checked) {
        lua_pushliteral(L, "checked");
        lua_pushinteger(L, (lua_Integer) peer->checked);
        lua_rawset(L, -3);
    }

    if (peer->down) {
        lua_pushliteral(L, "down");
        lua_pushboolean(L, 1);
        lua_rawset(L, -3);
    }

    return 0;
}


static ngx_http_upstream_main_conf_t *
ngx_http_lua_upstream_get_upstream_main_conf(lua_State *L)
{
    ngx_http_request_t                   *r;

    r = ngx_http_lua_get_request(L);

    if (r == NULL) {
        return ngx_http_cycle_get_module_main_conf(ngx_cycle,
                                                   ngx_http_upstream_module);
    }

    return ngx_http_get_module_main_conf(r, ngx_http_upstream_module);
}


static ngx_http_upstream_srv_conf_t *
ngx_http_lua_upstream_find_upstream(lua_State *L, ngx_str_t *host)
{
    u_char                               *port;
    size_t                                len;
    ngx_int_t                             n;
    ngx_uint_t                            i;
    ngx_http_upstream_srv_conf_t        **uscfp, *uscf;
    ngx_http_upstream_main_conf_t        *umcf;

    umcf = ngx_http_lua_upstream_get_upstream_main_conf(L);
    uscfp = umcf->upstreams.elts;

    for (i = 0; i < umcf->upstreams.nelts; i++) {

        uscf = uscfp[i];

        if (uscf->host.len == host->len
            && ngx_memcmp(uscf->host.data, host->data, host->len) == 0)
        {
            return uscf;
        }
    }

    port = ngx_strlchr(host->data, host->data + host->len, ':');
    if (port) {
        port++;
        n = ngx_atoi(port, host->data + host->len - port);
        if (n < 1 || n > 65535) {
            return NULL;
        }

        /* try harder with port */

        len = port - host->data - 1;

        for (i = 0; i < umcf->upstreams.nelts; i++) {

            uscf = uscfp[i];

            if (uscf->port
                && uscf->port == n
                && uscf->host.len == len
                && ngx_memcmp(uscf->host.data, host->data, len) == 0)
            {
                return uscf;
            }
        }
    }

    return NULL;
}
