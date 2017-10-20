/**
 * the pfifo_fast scheduler of traffic control module.
 * see linux/net/sched/sch_generic.c
 *
 * Lei Chen <raychen@qiyi.com>, Aug. 2017, initial.
 */
#include <assert.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include "netif.h"
#include "tc/tc.h"
#include "conf/tc.h"

#define TC_PRIO_MAX             15

static const uint8_t prio2band[TC_PRIO_MAX + 1] = {
	1, 2, 2, 2, 1, 2, 0, 0 , 1, 1, 1, 1, 1, 1, 1, 1
};

#define PFIFO_FAST_BANDS        3

static const int bitmap2band[] = {-1, 0, 1, 0, 2, 0, 1, 0};

struct pfifo_fast_priv {
    uint32_t bitmap;
    struct tc_mbuf_head q[PFIFO_FAST_BANDS];
};

static inline struct tc_mbuf_head *band2list(struct pfifo_fast_priv *priv,
					                         int band)
{
    assert(band >= 0 && band < PFIFO_FAST_BANDS);

    return priv->q + band;
}

static int pfifo_fast_enqueue(struct Qsch *sch, struct rte_mbuf *mbuf)
{
    int band, err;
    uint8_t prio;
    struct pfifo_fast_priv *priv;
    struct tc_mbuf_head *qh;

    /* sch->limit is same as dev->txq_desc_nb */
    if (sch->q.qlen >= sch->limit)
        return qsch_drop(sch, mbuf);

    /*
     * XXX:
     * no way to store/retrieve priority (e.g., TOS) to/from mbuf struct,
     * peek the IP header for tos fields. don't worry about
     * vlan, here vlan tag is not inserted yet.
     */
    if (likely(mbuf->packet_type == ETH_P_IP)) {
        struct iphdr *iph;

        iph = rte_pktmbuf_mtod_offset(mbuf, struct iphdr *,
                                      sizeof(struct ethhdr));

        prio = (iph->tos >> 1) & TC_PRIO_MAX;
    } else {
        prio = 0;
    }

    band = prio2band[prio];
    priv = qsch_priv(sch);
    qh = band2list(priv, band);
    
    err = __qsch_enqueue_tail(sch, mbuf, qh);
    if (err == EDPVS_OK) {
        priv->bitmap |= (1 << band);
        sch->q.qlen++;
    }
    return err;
}

static struct rte_mbuf *pfifo_fast_dequeue(struct Qsch *sch)
{
    struct pfifo_fast_priv *priv = qsch_priv(sch);
    int band = bitmap2band[priv->bitmap];
    struct tc_mbuf_head *qh;
    struct rte_mbuf *mbuf;

    if (unlikely(band < 0))
        return NULL;

    qh = band2list(priv, band);
    mbuf = __qsch_dequeue_head(sch, qh);

    if (mbuf)
        sch->q.qlen--;

    if (qh->qlen == 0)
        priv->bitmap &= ~(1 << band);

    return mbuf;
}

static struct rte_mbuf *pfifo_fast_peek(struct Qsch *sch)
{
    struct pfifo_fast_priv *priv = qsch_priv(sch);
    int band = bitmap2band[priv->bitmap];
    struct tc_mbuf_head *qh;
    struct tc_mbuf *tm;

    if (unlikely(band < 0))
        return NULL;

    qh = band2list(priv, band);
    tm = list_first_entry(&qh->mbufs, struct tc_mbuf, list);
    if (tm)
        return tm->mbuf;
    else
        return NULL;
}

static int pfifo_fast_init(struct Qsch *sch, const void *arg)
{
    int band;
    struct pfifo_fast_priv *priv = qsch_priv(sch);

    for (band = 0; band < PFIFO_FAST_BANDS; band++)
        tc_mbuf_head_init(band2list(priv, band));

    sch->limit = qsch_dev(sch)->txq_desc_nb;
    return EDPVS_OK;
}

static void pfifo_fast_reset(struct Qsch *sch)
{
    int band;
    struct pfifo_fast_priv *priv = qsch_priv(sch);

    for (band = 0; band < PFIFO_FAST_BANDS; band++)
        __qsch_reset_queue(sch, band2list(priv, band));

    priv->bitmap = 0;
    sch->q.qlen = 0;
}

static int pfifo_fast_dump(struct Qsch *sch, void *arg)
{
    struct tc_prio_qopt *qopt = arg;

    qopt->bands = PFIFO_FAST_BANDS;
    memcpy(qopt->priomap, prio2band, sizeof(qopt->priomap));

    return EDPVS_OK;
}

struct Qsch_ops pfifo_fast_ops = {
    .name       = "pfifo_fast",
    .priv_size  = sizeof(struct pfifo_fast_priv),
    .enqueue    = pfifo_fast_enqueue,
    .dequeue    = pfifo_fast_dequeue,
    .peek       = pfifo_fast_peek,
    .init       = pfifo_fast_init,
    .reset      = pfifo_fast_reset,
    .dump       = pfifo_fast_dump,
};
