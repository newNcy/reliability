#pragma once
#include "kcp/ikcp.h"
#include "encoding.h"
#include <stdint.h>
#include <stdio.h>

#define CHANNEL_COUNT 32
#define MTU 1400

enum class Reliability
{
    UNRELIABLE,
    UNRELIABLE_SEQUENCED,
    RELIABLE_ORDERED = 3, 
    RELIABLE_SEQUENCED,
};

struct PackQueneNode
{
    PackQueneNode * prev, *next;
    uint32_t len;
    char * data;
};

static inline PackQueneNode * queueNodeAlloc(uint32_t size)
{
    PackQueneNode * node = new PackQueneNode;
    node->prev = node;
    node->next = node;
    node->len = size;
    node->data = new char[size];

    return node;
}

static inline void queueNodeFree(PackQueneNode * node)
{
    if (node)
    {
        if (node->data)
        {
            delete [] node->data;
            node->len = 0;
        }
        delete node;
    }
}


static inline void queueAddTail(PackQueneNode * head, PackQueneNode * node)
{
    head->prev->next = node;
    node->next = head;
    node->prev = head->prev;
    head->prev = node;

}

static inline void queueDel(PackQueneNode * node)
{
    node->next->prev = node->prev;
    node->prev->next = node->next;
    node->prev = nullptr;
    node->next = nullptr;
}

static inline bool queueEmpty(PackQueneNode * head)
{
    return head->next == head;
}

static inline void queueShow(PackQueneNode * head)
{
    PackQueneNode * n = head->next;
    printf("[ ");
    while (n != head) {
        printf("%d ", n->len);
        n = n->next;
    }
    printf("]\n");
}

struct ReliabilityLayer
{
    ReliabilityLayer(uint32_t conv, void * user);
    ~ReliabilityLayer();
    void Send(const void* messageData, unsigned int messageSize, Reliability reliability = Reliability::RELIABLE_ORDERED, char channel = 0);
    int Receive(void * buffer, uint32_t len);
    void ProcessMessage(const void * data, uint32_t length);

    uint32_t channelSeqHighest_[CHANNEL_COUNT] = {0};
    uint32_t channelSeqNext_[CHANNEL_COUNT] = {0};

    ikcpcb * kcp_ = nullptr;
    void * user = nullptr;
	int (*output)(const char *buf, int len, ReliabilityLayer * reliabilityLayer, void *user);
    byte buffer_[MTU] = {0};


    PackQueneNode recvHead_;
};
