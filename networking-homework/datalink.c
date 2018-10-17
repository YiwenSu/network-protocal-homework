#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "protocol.h"
#include "datalink.h"
#include <stdbool.h>

#define DATA_TIMER  3000
#define MAX_SEQ 15
#define ACK_TIMER 800
#define NR_BUFS ((MAX_SEQ + 1) / 2)     //缓冲区大小

bool no_nak = true;
int oldest_frame = MAX_SEQ + 1;

typedef struct FRAME {
	
	unsigned char kind;
	unsigned char ack;
	unsigned char seq;
	unsigned char data[PKT_LEN];
	unsigned int  padding;
}frame;


static int phl_ready = 0;

static bool between(unsigned char a, unsigned char b, unsigned char c)

{
	return (((a <= b) && (b < c)) || ((c < a) && (a <= b)) || ((b < c) && (c < a)));
			
}

static void put_frame(unsigned char *frame, int len)
{
	*(unsigned int *)(frame + len) = crc32(frame, len);
	send_frame(frame, len + 4);
	phl_ready = 0;
}

static void send_data(unsigned char fk, unsigned char frame_nr, unsigned char frame_expected, unsigned char buffer[NR_BUFS][PKT_LEN])
{
	frame s;
	s.kind = fk;
	s.seq = frame_nr;
	s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);

	if (fk == FRAME_DATA)
	{
		memcpy(s.data, buffer[frame_nr % NR_BUFS], PKT_LEN);
		put_frame((unsigned char *)&s, 3 + PKT_LEN);
		//dbg_frame("发送帧 %d  ack:%d, ID %d\n", s.seq, s.ack, *(short *)s.data);
		start_timer(frame_nr % NR_BUFS, DATA_TIMER);
		
	}
	s.seq = frame_nr;
	s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);

	if (fk == FRAME_NAK)
	{
		no_nak = false;
		put_frame((unsigned char *)&s, 3);
	}
	if (fk == FRAME_ACK)
	{
		
		put_frame((unsigned char *)&s, 3);//发送
		//dbg_frame("发送ACK  %d\n", s.ack);//输出记录
	}
	
	stop_ack_timer();
}

int main(int argc, char **argv)
{
	unsigned char ack_expected = 0, next_frame_to_send = 0,frame_expected = 0, too_far = NR_BUFS;
	int event, arg;
	int i;
	frame f;
	unsigned char out_buf[NR_BUFS][PKT_LEN], in_buf[NR_BUFS][PKT_LEN];
	int len = 0; 
	static unsigned char nbuffered;
	bool arrived[NR_BUFS];
	
	protocol_init(argc, argv);

	lprintf("Coded by syw, Build Time: " __DATE__"  "__TIME__"\n");


	for (i = 0; i < NR_BUFS; i++)
		arrived[i] =0;
	
	while (1)
	{   
		event = wait_for_event(&arg);			
		switch (event)
		{
		case NETWORK_LAYER_READY:
			nbuffered++;                   
			get_packet(out_buf[next_frame_to_send % NR_BUFS]);
			send_data(FRAME_DATA, next_frame_to_send, frame_expected, out_buf);
			next_frame_to_send = (next_frame_to_send + 1) % (MAX_SEQ + 1);
			
			break;

		case PHYSICAL_LAYER_READY:
			phl_ready = 1;
			break;

		case FRAME_RECEIVED:
			len = recv_frame((unsigned char *)&f, sizeof f);
			if (len <= 4 || crc32((unsigned char *)&f, len) != 0)
			{	
				if (no_nak == 1)
					send_data(FRAME_NAK, 0, frame_expected, out_buf);
				//dbg_frame("发送nak\n");
				break;
			}
			if (f.kind == FRAME_DATA)
			{
				
				if ((f.seq != frame_expected) && no_nak == 1)
					send_data(FRAME_NAK, 0, frame_expected, out_buf);
				else
					start_ack_timer(ACK_TIMER);
				if (between(frame_expected, f.seq, too_far) == 1 && arrived[f.seq % NR_BUFS] == 0)
				{
					
					
					arrived[f.seq % NR_BUFS] = 1;
					memcpy(in_buf[f.seq % NR_BUFS], f.data, len-7);
					//dbg_frame("收到 DATA %d %d, ID %d\n", f.seq, f.ack, *(short *)f.data);
					while (arrived[frame_expected % NR_BUFS])
					{
						
						put_packet(in_buf[frame_expected % NR_BUFS], len - 7);

						no_nak = 1;

						arrived[frame_expected%NR_BUFS] = 0;

						too_far = (too_far + 1) % (MAX_SEQ + 1);
						frame_expected = (frame_expected + 1) % (MAX_SEQ + 1);
						
						start_ack_timer(ACK_TIMER);
					}
				}
			}
			if ((f.kind == FRAME_NAK) && between(ack_expected, (f.ack + 1) % (MAX_SEQ + 1), next_frame_to_send) == 1)
			{
				send_data(FRAME_DATA, (f.ack + 1) % (MAX_SEQ + 1), frame_expected, out_buf);
			}
			while (between(ack_expected, f.ack, next_frame_to_send) == 1)
			{
				nbuffered=nbuffered-1;		
				stop_timer(ack_expected % NR_BUFS);			
				ack_expected = (ack_expected + 1) % (MAX_SEQ + 1);
			}
			break;

		case DATA_TIMEOUT:
			//dbg_event("DATA %d 超时\n", arg);
			send_data(FRAME_DATA, ack_expected, frame_expected, out_buf);
			break;
		case ACK_TIMEOUT:
			//dbg_event("ACK %d 超时\n", arg);
			send_data(FRAME_ACK, 0, frame_expected, out_buf);
		}

		if (nbuffered < NR_BUFS && phl_ready)
			
			enable_network_layer();
		else
			
			disable_network_layer();
	}
	system("pause");
}
