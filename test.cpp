
#include "kcp/test.h"
#include "reliability.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <stdio.h>
#include <stdlib.h>

#include <random>
#include <map>
#include <set>
#include <vector>


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
    std::mt19937 engine;
    Net(int lostrate, int delaymin, int delaymax): lostrate(lostrate), delaymin(delaymin), delaymax(delaymax), engine(time(nullptr)) { }

    void send(int dst, const void * buff, uint32_t length)
    {
        std::lock_guard<std::mutex> _(netmtx);
        std::uniform_int_distribution<int> dist(1, 100);
        int lost = dist(engine);
        if ( lost< lostrate) {
            printf("lost pack to %d %d bytes lost=%d\n", dst, length, lost);
            return ;
        }
        printf("send to %d %d bytes\n", dst, length);
        Packet pack;
        pack.data = new char[length];
        pack.len = length;

        memcpy(pack.data, buff, length);

        pack.ts = iclock() + delaymin;
        pack.ts += rand() % (delaymax - delaymin + 1);
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


Net net(20, 60, 125);
bool clientFinish = false;



int layer_output(const char * buff, int len, ReliabilityLayer * reliabilityLayer,  void * ud)
{
    //printf("send %d bytes to %llu\n", len, ud);
    int target = (long long)ud;
    net.send(target, buff, len);
    return len;
}

int kcp_output(const char * buff, int len, ikcpcb * kcp, void * ud)
{
    int target = (long long)ud;
    net.send(target, buff, len);
    return len;
}



void server()
{
    ikcpcb * kcp = ikcp_create(1, (void*)1);

    kcp->output = kcp_output;
    ikcp_wndsize(kcp, 128, 128);
    ikcp_nodelay(kcp, 1, 10, 2, 1);
    /*
    ReliabilityLayer * reliabilityLayer = new ReliabilityLayer(1, (void*)1);
    reliabilityLayer->output = layer_output;
    ikcp_wndsize(reliabilityLayer->kcp_, 128, 128);
    ikcp_nodelay(reliabilityLayer->kcp_, 1, 3, 2, 1);
    reliabilityLayer->kcp_->interval =  10;
    */
    printf("server start\n");

    int count = 0;
    int raw = 0;
    std::vector<uint32_t> recv;
    while (!clientFinish) {
        auto now = iclock();
        ikcp_update(kcp, now);
        char buff[MTU] = {0};
        int rc = net.recv(0, buff, MTU);
        if (rc > 0) {
            raw ++;
            fflush(stdout);
            ikcp_input(kcp, buff, rc);
            //reliabilityLayer->ProcessMessage(buff, rc);
        }

        while (true) {
            int ret = ikcp_recv(kcp, buff, MTU);
            if (ret <= 0) {
                break;
            }
            count ++;
            uint32_t sn = 0;
            decode32u((byte*)buff, &sn);
            recv.push_back(sn); 
            printf("--->");
            for (auto sn : recv) {
                printf("%u ", sn);
            }
            printf("\n");
            fflush(stdout);
            //printf("[s:%u] recv %d\n", iclock(), sn);
        }
        fflush(stdout);
    }
    printf("server end, recv %d packets %d dgram\n", count, raw);
}

void client()
{
    ikcpcb * kcp = ikcp_create(1, (void*)0);

    kcp->output = kcp_output;
    ikcp_wndsize(kcp, 128, 128);
    ikcp_nodelay(kcp, 1, 10, 2, 1);
    /*
    ReliabilityLayer * reliabilityLayer = new ReliabilityLayer(1, (void*)0);
    reliabilityLayer->output = layer_output;
    ikcp_wndsize(reliabilityLayer->kcp_, 128, 128);
    ikcp_nodelay(reliabilityLayer->kcp_, 1, 5, 2, 1);
    */
    printf("client start\n");

    int sn = 1;
    int limit = 9;
    auto last = iclock();
    for (;;) {
        //net.send(0, &i, sizeof(i));
        auto now = iclock();
        ikcp_update(kcp, now);
        if (sn <= limit && now - last > 10) {
            last = now;
            //reliabilityLayer->Send(&sn, sizeof(sn), sn % 10==0? Reliability::RELIABLE_ORDERED : Reliability::RELIABLE_SEQUENCED, 0);
            ikcp_send(kcp, (char*)&sn, sizeof(sn), IKCP_UNRELIABLE_SEQUENCED, 0);
            ikcp_flush(kcp);
            printf("[c:%u] send %d\n", now, sn);
            sn ++ ;
        }
        char buff[MTU] = {0};
        int rc = net.recv(1, buff, MTU);
        if (rc > 0) {
            ikcp_input(kcp, buff, rc);
        }
        fflush(stdout);
        isleep(5);
        if (sn > limit && ikcp_waitsnd(kcp) == 0) {
            break;
        } else if (sn > limit){
        }
    }

    isleep(1000);
    clientFinish = true;
    printf("client end\n");
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
