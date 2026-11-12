/* Paket gözlemleri benzetilir; gerçek WinDivert yakalaması başlatılmaz. */
#include "../../engine/desync.c"
#include <assert.h>

int main(int argc, char **argv)
{
    UINT32 ip[4] = { 0x01020304, 0, 0, 0 };
    WINDIVERT_TCPHDR tcp = {0};
    DWORD now;
    char line[256];
    FILE *f;
    autolist_init("approved.txt");
    autolist_note_clienthello(0, ip, htons(443), htons(1234), htonl(100), 150, "www.google.com", 14);
    probes[0].tick -= 1000;
    autolist_note_clienthello(0, ip, htons(443), htons(1234), htonl(100), 150, "www.google.com", 14);
    assert(npending == 0); /* loss/retransmission is not blocking */
    tcp.SrcPort = htons(443); tcp.DstPort = htons(1234);
    tcp.Ack = 1; tcp.AckNum = htonl(250);
    now = GetTickCount();
    autolist_observe(ip, 0, &tcp, 100, now); /* successful server data */
    tcp.Rst = 1;
    autolist_observe(ip, 0, &tcp, 0, now + 1);
    assert(npending == 0); /* normal close after response */
    autolist_note_clienthello(0, ip, htons(443), htons(1234), htonl(200), 150, "candidate.example", 17);
    tcp.AckNum = htonl(999);
    autolist_observe(ip, 0, &tcp, 0, GetTickCount());
    assert(npending == 0); /* wrong reset acknowledgement */
    tcp.AckNum = htonl(350);
    autolist_observe(ip, 0, &tcp, 0, GetTickCount());
    assert(npending == 1);
    autolist_poll();
    assert(!blackwhitelist_check_hostname("candidate.example", 17));
    f = fopen("approved.txt", "r"); assert(f);
    assert(fgets(line, sizeof(line), f) == NULL); fclose(f);
    f = fopen("approved.txt.candidates.txt", "r"); assert(f);
    assert(fgets(line, sizeof(line), f) && strstr(line, "candidate.example")); fclose(f);
    puts("autolist: retransmission, successful response, unrelated reset and candidate isolation OK");
    return 0;
}
