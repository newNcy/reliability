
#include "kcp/test.h"
#include "encoding.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <stdio.h>
#include <stdlib.h>

#include <random>
#include <map>
#include <set>
#include <vector>
#define MTU 1400


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
    std::map<int, uint32_t> recvCounter;
    std::atomic<int> tx;
    std::mt19937 engine;
    Net(int lostrate, int delaymin, int delaymax): lostrate(lostrate), delaymin(delaymin), delaymax(delaymax), engine(time(nullptr)) { }

    void send(int dst, const void * buff, uint32_t length)
    {
        std::lock_guard<std::mutex> _(netmtx);
        auto it = recvCounter.find(dst);
        if (it == recvCounter.end()) {
            recvCounter[dst] = length;
        }else {
            it->second += length;
        }
        std::uniform_int_distribution<int> dist(1, 100);
        int lost = dist(engine);
        if ( lost< lostrate) {
            //printf("lost pack to %d %d bytes lost=%d\n", dst, length, lost);
            return ;
        }
        //printf("send to %d %d bytes\n", dst, length);
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
    ~Net() {
        for (auto it = recvCounter.begin(); it != recvCounter.end(); it ++) {
            printf("%d recv %d bytes\n", it->first, it->second);
        }
    }
};


Net net(20, 60, 125);
bool clientFinish = false;



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
            //printf("[s:%u] recv %d\n", iclock(), sn);
        }
        fflush(stdout);
    }
    printf("--->");
    for (auto sn : recv) {
        printf("%u ", sn);
    }
    printf("\n");
    fflush(stdout);
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
    int limit = 0xfffff;
    auto last = iclock();
    for (;;) {
        //net.send(0, &i, sizeof(i));
        auto now = iclock();
        ikcp_update(kcp, now);
        if (sn <= limit && now - last > 5) {
            last = now;
            //reliabilityLayer->Send(&sn, sizeof(sn), sn % 10==0? Reliability::RELIABLE_ORDERED : Reliability::RELIABLE_SEQUENCED, 0);
            unsigned char reliability = IKCP_RELIABLE_SEQUENCED;
            if (sn % 10 == 9) {
                reliability = IKCP_UNRELIABLE_SEQUENCED;
            } else if (sn  % 10 == 0) {
                reliability = IKCP_RELIABLE_ORDERED;
            }
            reliability = IKCP_RELIABLE_ORDERED;
            ikcp_send(kcp, (char*)&sn, sizeof(sn), reliability, 0);
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

long _itimediff(IUINT32 later, IUINT32 earlier) ;
long diffu16(IUINT16 later, IUINT16  earlier) 
{
    return (IINT16)(later - earlier);
}

int main() 
{
    IUINT32 a = 0;
    IUINT32 b = 0xffffff;
    printf("%d %d\n", diffu16(a, b) > 0, diffu16(b, a) > 0); 
    srand(time(nullptr));

    std::thread st(server);
    std::thread ct(client);

    ct.join();
    st.join();

    printf("simulate end\n");
    return 0;
}
