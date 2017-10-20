/**
 * traffic control classifier of dpip tool.
 * see iproute2 "tc filter".
 *
 * raychen@qiyi.com, Aug. 2017, initial.
 */
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "match.h"
#include "dpip.h"
#include "sockopt.h"
#include "conf/tc.h"

static void cls_help(void)
{
    fprintf(stderr,
        "Usage:\n"
        "    dpip cls { add | del | change | replace | show } dev STRING\n"
        "             [ handle HANDLE ] [ qsch HANDLE ]\n"
        "             [ pkttype PKTTYPE ] [ prio PRIO ]\n"
        "             [ CLS_TYPE [ COPTIONS ] ]\n"
        "\n"
        "Parameters:\n"
        "    PKTTYPE    := { ipv4 | vlan }\n"
        "    CLS_TYPE   := { match }\n"
        "    COPTIONS   := { MATCH_OPTS }\n"
        "    PRIO       := NUMBER\n"
        "\n"
        "Match options:\n"
        "    MATCH_OPTS := pattern PATTERN { target { CHILD_QSCH | drop } }\n"
        "    PATTERN    := comma seperated of tokens below,\n"
        "                  { PROTO | SRANGE | DRANGE | IIF | OIF }\n"
        "    CHILD_QSCH := child qsch handle of the qsch cls attached.\n"
        "    PROTO      := \"{ tcp | udp }\"\n"
        "    SRANGE     := \"from=RANGE\"\n"
        "    DRANGE     := \"to=RANGE\"\n"
        "    RANGE      := ADDR[-ADDR][:PORT[-PORT]]\n"
        "    IIF        := \"iif=IFNAME\"\n"
        "    OIF        := \"oif=IFNAME\"\n"
        "\n"
        "Examples:\n"
        "    dpip cls show dev eth0\n"
        "    dpip cls add dev eth0 qsch 1:0 prio 255 target 1:1 \\\n"
        "         match 'tcp,from=192.168.0.1:1-1024,oif=eth1'\n"
        "    dpip cls add dev eth0 qsch 1:0 pkttype vlan target 1:2 \\\n"
        "         match 'udp,from=192.168.0.1-192.168.0.20'\n"
        "    dpip cls del dev eth0 handle 10:1\n"
        );
}

static void cls_dump_param(const char *ifname, const union tc_param *param)
{
    const struct tc_cls_param *cls = &param->cls;
    char handle[16], sch_id[16];

    printf("cls %s %s: dev %s %s pkttype 0x%08x prio %d",
           cls->kind, tc_handle_itoa(cls->handle, handle, sizeof(handle)),
           ifname, tc_handle_itoa(cls->sch_id, sch_id, sizeof(sch_id)),
           cls->pkt_type, cls->priority);

    if (strcmp(cls->kind, "match") == 0) {
        char result[32], patt[256];
        const struct tc_cls_match_copt *m = &cls->copt.match;

        if (m->result.drop)
            snprintf(result, sizeof(result), "%s", "drop");
        else
            snprintf(result, sizeof(result), "%u", m->result.sch_id);

        printf("%s target %s",
               dump_match(m->proto, &m->match, patt, sizeof(patt)), result);
    }

    printf("\n");
}

static int cls_parse(struct dpip_obj *obj, struct dpip_conf *cf)
{
    struct tc_conf *conf = obj->param;
    struct tc_cls_param *param = &conf->param.cls;

    memset(param, 0, sizeof(*param));

    /* default values */
    param->pkt_type = ETH_P_IP;
    param->handle = TC_H_UNSPEC;
    param->sch_id = TC_H_ROOT;
    param->priority = 0;

    while (cf->argc > 0) {
        if (strcmp(CURRARG(cf), "dev") == 0) {
            NEXTARG_CHECK(cf, CURRARG(cf));
            snprintf(conf->ifname, IFNAMSIZ, "%s", CURRARG(cf));
        } else if (strcmp(CURRARG(cf), "handle") == 0) {
            NEXTARG_CHECK(cf, CURRARG(cf));
            param->handle = tc_handle_atoi(CURRARG(cf));
        } else if (strcmp(CURRARG(cf), "qsch") == 0) {
            NEXTARG_CHECK(cf, CURRARG(cf));
            param->sch_id = atoi(CURRARG(cf));
        } else if (strcmp(CURRARG(cf), "pkttype") == 0) {
            NEXTARG_CHECK(cf, CURRARG(cf));
            if (strcasecmp(CURRARG(cf), "ipv4") == 0)
                param->pkt_type = ETH_P_IP;
            else if (strcasecmp(CURRARG(cf), "vlan") == 0)
                param->pkt_type = ETH_P_8021Q;
            else {
                fprintf(stderr, "pkttype not support\n");
                return EDPVS_INVAL;
            }
        } else if (strcmp(CURRARG(cf), "prio") == 0) {
            NEXTARG_CHECK(cf, CURRARG(cf));
            param->priority = atoi(CURRARG(cf));
        } else if (strcmp(CURRARG(cf), "match") == 0) {
            NEXTARG_CHECK(cf, CURRARG(cf));
            snprintf(param->kind, TCNAMESIZ, "%s", "match");
        } else { /* kind must be set adead then COPTIONS */
            if (strcmp(param->kind, "match") == 0) {
                struct tc_cls_match_copt *m = &param->copt.match;

                if (strcmp(CURRARG(cf), "pattern") == 0) {
                    NEXTARG_CHECK(cf, CURRARG(cf));
                    if (parse_match(CURRARG(cf), &m->proto,
                                    &m->match) != EDPVS_OK) {
                        fprintf(stderr, "invalid pattern: %s\n", CURRARG(cf));
                        return EDPVS_INVAL;
                    }
                } else if (strcmp(CURRARG(cf), "target") == 0) {
                    NEXTARG_CHECK(cf, CURRARG(cf));
                    if (strcmp(CURRARG(cf), "drop") == 0)
                        m->result.drop = true;
                    else
                        m->result.sch_id = atoi(CURRARG(cf));
                }
            } else {
                fprintf(stderr, "invalid/miss cls type: `%s'\n", param->kind);
                return EDPVS_INVAL;
            }
        }
    }

    return EDPVS_OK;
}

static int cls_check(const struct dpip_obj *obj, dpip_cmd_t cmd)
{
    const struct tc_conf *conf = obj->param;
    const struct tc_cls_param *param = &conf->param.cls;

    if (!strlen(conf->ifname)) {
        fprintf(stderr, "missing device.\n");
        return EDPVS_INVAL;
    }

    switch (cmd) {
    case DPIP_CMD_REPLACE:
        if (!param->handle)
            goto missing_handle;
        /* fall through */

    case DPIP_CMD_ADD:
        if (param->sch_id == TC_H_UNSPEC) {
            fprintf(stderr, "which qsch to attach ?\n");
            return EDPVS_INVAL;
        }

        if (strcmp(param->kind, "match") == 0) {
            if (is_empty_match(&param->copt.match.match)) {
                fprintf(stderr, "invalid match pattern.\n");
                return EDPVS_INVAL;
            }
        } else {
            fprintf(stderr, "invalid cls kind.\n");
            return EDPVS_INVAL;
        }
        break;

    case DPIP_CMD_DEL:
        if (!param->handle)
            goto missing_handle;
        break;

    case DPIP_CMD_SET:
        if (!param->handle)
            goto missing_handle;

        if (strcmp(param->kind, "match") == 0) {
            if (is_empty_match(&param->copt.match.match)) {
                fprintf(stderr, "invalid match pattern.\n");
                return EDPVS_INVAL;
            }
        } else {
            fprintf(stderr, "invalid cls kind.\n");
            return EDPVS_INVAL;
        }

        break;

    case DPIP_CMD_SHOW:
        break;

    default:
        return EDPVS_NOTSUPP;
    }

    return EDPVS_OK;

missing_handle:
    fprintf(stderr, "missing handle.\n");
    return EDPVS_INVAL;
}

static int cls_do_cmd(struct dpip_obj *obj, dpip_cmd_t cmd,
                      struct dpip_conf *conf)
{
    struct tc_conf *tc_conf = obj->param;
    union tc_param *params;
    int err, i;
    size_t size;

    switch (cmd) {
    case DPIP_CMD_ADD:
        return dpvs_setsockopt(SOCKOPT_TC_ADD, tc_conf,
                               sizeof(struct tc_conf));
    case DPIP_CMD_DEL:
        return dpvs_setsockopt(SOCKOPT_TC_DEL, tc_conf,
                               sizeof(struct tc_conf));
    case DPIP_CMD_SET:
        return dpvs_setsockopt(SOCKOPT_TC_CHANGE, tc_conf,
                               sizeof(struct tc_conf));
    case DPIP_CMD_REPLACE:
        return dpvs_setsockopt(SOCKOPT_TC_REPLACE, tc_conf,
                               sizeof(struct tc_conf));
    case DPIP_CMD_SHOW:
        err = dpvs_getsockopt(SOCKOPT_TC_SHOW, tc_conf,
                              sizeof(struct tc_conf), (void **)&params, &size);
        if (err != 0)
            return EDPVS_INVAL;

        if (size < 0 || size % sizeof(*params) != 0) {
            fprintf(stderr, "corrupted response.\n");
            dpvs_sockopt_msg_free(params);
            return EDPVS_INVAL;
        }

        for (i = 0; i < size / sizeof(*params); i++)
            cls_dump_param(tc_conf->ifname, &params[i]);

        dpvs_sockopt_msg_free(params);
        return EDPVS_OK;
    default:
        return EDPVS_NOTSUPP;
    }
}

static struct tc_conf cls_conf = {
    .obj    = TC_OBJ_CLS,
};

static struct dpip_obj dpip_cls = {
    .name   = "cls",
    .param  = &cls_conf,
    .help   = cls_help,
    .parse  = cls_parse,
    .check  = cls_check,
    .do_cmd = cls_do_cmd,
};

static void __init cls_init(void)
{
    dpip_register_obj(&dpip_cls);
}

static void __exit cls_exit(void)
{
    dpip_unregister_obj(&dpip_cls);
}
