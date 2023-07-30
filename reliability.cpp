#include "reliability.h"
#include <string.h>

static int wrap_output(const char *buf, int len, struct IKCPCB *kcp, void *user)
{
    ReliabilityLayer * reliabilityLayer = static_cast<ReliabilityLayer*>(user);
    char frame[MTU * 2] = {0};
    encode8u((byte*)frame, 2);
    memcpy(frame + 1, buf, len);
    assert(reliabilityLayer->output);
    return reliabilityLayer->output(frame, len + 1, reliabilityLayer, reliabilityLayer->user);
}

ReliabilityLayer::ReliabilityLayer(uint32_t conv, void * user)
{
    kcp_ = ikcp_create(conv, this);
    this->user = user;
    kcp_->output = wrap_output;
    recvHead_.next = &recvHead_;
    recvHead_.prev = &recvHead_;
}

ReliabilityLayer::~ReliabilityLayer()
{
    if (kcp_)
    {
        ikcp_release(kcp_);
        kcp_ = nullptr;
    }

    while(!queueEmpty(&recvHead_)) 
    {
        queueDel(recvHead_.next);
    }
}




void ReliabilityLayer::Send(const void * message, uint32_t length, Reliability reliability, char channel)
{
    assert(kcp_);
    assert(kcp_->output);


    if (channel < 0) 
    {
        channel = 0;
    }

    if (channel > CHANNEL_COUNT-1) 
    {
        channel = CHANNEL_COUNT - 1;
    }

    if (reliability < Reliability::RELIABLE_ORDERED && length > (MTU - sizeof(uint32_t) - 2)) 
    {
        reliability = Reliability::RELIABLE_ORDERED;
    }

    byte * p = buffer_;
    if (reliability == Reliability::UNRELIABLE) 
    {
        p = encode8u(p, 0);
        memcpy(p, message, length);
        output((char*)buffer_, length + 1, this, user);
    }
    else if (reliability == Reliability::UNRELIABLE_SEQUENCED)
    {
        p = encode8u(p, 1);
        p = encode8u(p, channel);
        p = encode32u(p, channelSeqNext_[channel] ++ );
        memcpy(p, message, length);
        output((char*)buffer_, length + (p-buffer_), this, user);
    }
    else 
    {
        ikcp_send(kcp_, (char*)message, length, reliability == Reliability::RELIABLE_SEQUENCED, channel);
    }
}

void ReliabilityLayer::ProcessMessage(const void * data, uint32_t length)
{

    assert(kcp_);
    if (!data || length == 0) 
    {
        return;
    }

    const byte * p = (const byte*)data;
    Reliability reliability = (Reliability)p[0];

    p ++;
    length --;

    if (reliability == Reliability::UNRELIABLE || reliability == Reliability::UNRELIABLE_SEQUENCED)
    {
        if (reliability == Reliability::UNRELIABLE_SEQUENCED)
        {
            if (length < 1 + sizeof(uint32_t)) 
            {
                return;
            }
            char channel = p[0];
            uint32_t sn = 0;
            p = decode32u(p + 1, &sn);
            length -= 1 + sizeof(uint32_t);

            if (channel < 0 || channel > CHANNEL_COUNT-1)
            {
                return;
            }

            if (_itimediff(sn, channelSeqHighest_[channel]) <= 0)
            {
                return;
            }
            
            channelSeqHighest_[channel] = sn;
        }
        PackQueneNode * node = queueNodeAlloc(length);
        memcpy(node->data, p, length);
        queueAddTail(&recvHead_, node);
        //queueShow(&recvHead_);
    }
    else 
    {
        ikcp_input(kcp_, (char*)p, length);
    }
    //printf("process %d %d bytes\n", (int)reliability, length);
}

int ReliabilityLayer::Receive(void * buffer, uint32_t len)
{
    assert(kcp_);
    if (queueEmpty(&recvHead_)) 
    {
        return ikcp_recv(kcp_, (char*)buffer, len);
    }
    
    PackQueneNode * node = recvHead_.next;

    if (len < node->len) 
    {
        return -3;
    }

    if (buffer)
    {
        memcpy(buffer, node->data, node->len);
        len = node->len;
        queueDel(node);
        queueNodeFree(node);
    }

    return len;
}
