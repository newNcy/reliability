
#include "kcp/test.h"
#include "reliability.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <stdio.h>

#include <map>
#include <set>


struct Packet
{
    char * data = nullptr;
    int len = 0;
    uint32_t ts = 0;
};

bool operator < (const Packet & a, const Packet & b)
{
    return a.ts < b.ts;
}

bool operator == (const Packet & a, const Packet & b)
{
    return &a == &b;
}

struct Net
{
    int lostrate;
    int delaymin, delaymax;
    std::mutex netmtx;
    std::map<int, std::multiset<Packet>> packBuff;
    std::atomic<int> tx;
    Net(int lostrate, int delaymin, int delaymax): lostrate(lostrate), delaymin(delaymin), delaymax(delaymax) { }

    void send(int dst, const void * buff, uint32_t length)
    {
        std::lock_guard<std::mutex> _(netmtx);
        if (rand() % 100 < lostrate) {
            return ;
        }
        Packet pack;
        pack.data = new char[length];
        pack.len = length;

        memcpy(pack.data, buff, length);

        pack.ts = iclock() + delaymin;
        pack.ts += rand() % (delaymax - delaymin);
        tx ++;
        packBuff[dst].insert(pack);
    };

    int recv(int dst, void * buff, uint32_t length)
    {
        std::lock_guard<std::mutex> _(netmtx);
        if (packBuff[dst].empty()) {
            return 0;
        }
        auto it = packBuff[dst].begin();
        if (it->ts <= iclock())
        {
            memcpy(buff, it->data, it->len);
            length = it->len;
            tx --;
            delete [] it->data;
            packBuff[dst].erase(it);
        } else
        {
            length = 0;
        }
        return length;
    }
};


Net net(10, 60, 125);
bool clientFinish = false;



int layer_output(const char * buff, int len, ReliabilityLayer * reliabilityLayer,  void * ud)
{
    //printf("send %d bytes to %llu\n", len, ud);
    int target = (long long)ud;
    net.send(target, buff, len);
    return len;
}



void server()
{
    ReliabilityLayer * reliabilityLayer = new ReliabilityLayer(1, (void*)1);
    reliabilityLayer->output = layer_output;
    ikcp_wndsize(reliabilityLayer->kcp_, 128, 128);
    ikcp_nodelay(reliabilityLayer->kcp_, 1, 10, 2, 1);
    reliabilityLayer->kcp_->interval =  10;
    printf("server start\n");

    int count = 0;
    int raw = 0;
    while (!clientFinish) {
        auto now = iclock();
        ikcp_update(reliabilityLayer->kcp_, now);
        char buff[MTU] = {0};
        int rc = net.recv(0, buff, MTU);
        if (rc > 0) {
            raw ++;
            reliabilityLayer->ProcessMessage(buff, rc);
        }

        if (reliabilityLayer->Receive(buff, MTU) > 0) {
            count ++;
            uint32_t sn = 0;
            decode32u((byte*)buff, &sn);
            printf("[s:%u] recv %d\n", iclock(), sn);
        }
        fflush(stdout);
    }
    printf("server end, recv %d packets %d dgram\n", count, raw);
    delete reliabilityLayer;
}

void client()
{
    ReliabilityLayer * reliabilityLayer = new ReliabilityLayer(1, (void*)0);
    reliabilityLayer->output = layer_output;
    ikcp_wndsize(reliabilityLayer->kcp_, 128, 128);
    ikcp_nodelay(reliabilityLayer->kcp_, 1, 10, 2, 1);
    printf("client start\n");

    int sn = 0;
    auto last = iclock();
    for (;;) {
        //net.send(0, &i, sizeof(i));
        auto now = iclock();
        if (sn < 100 && now - last > 10) {
            last = now;
            reliabilityLayer->Send(&sn, sizeof(sn), Reliability::RELIABLE_ORDERED, 0);
            printf("[c:%u] send %d\n", now, sn);
            sn ++ ;
        }
        ikcp_update(reliabilityLayer->kcp_, now);
        ikcp_flush(reliabilityLayer->kcp_);
        char buff[MTU] = {0};
        int rc = net.recv(1, buff, MTU);
        if (rc > 0) {
            reliabilityLayer->ProcessMessage(buff, rc);
        }
        isleep(5);

    }

    isleep(5000);
    while (net.tx > 0) {
        int tx = net.tx;
        printf("%d packet in net\n", tx);
    }

    clientFinish = true;
    printf("client end\n");
    delete reliabilityLayer;
}


int main() 
{
    srand(time(nullptr));

    std::thread st(server);
    std::thread ct(client);

    ct.join();
    st.join();

    printf("simulate end\n");
    return 0;
}
