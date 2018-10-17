#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"

#define MAX_SEQ    7                      //���֡���
#define DATA_TIMER 3000                   //DATA_TIMERʱ��            
#define inc(k) if(k<MAX_SEQ)k++;else k=0  //ѭ����1

typedef enum {false, true} bool;          //��������
typedef unsigned char seq_nr;             //֡��Ż�ȷ�Ϻ�

typedef struct 
{
	unsigned char info[PKT_LEN];
}packet;                                  //����

static int phl_ready = 0;                 //�����������λ

typedef struct
{
    seq_nr ack;                           //ȷ�Ϻ�
	seq_nr seq;                           //֡���
    packet data;                          //��������
    unsigned int  padding;                //CRCУ���
}frame;


//��֡����ʱ���жϸ�֡�Ƿ����ڽ��շ�������
static bool between (seq_nr a, seq_nr b, seq_nr c)
{
	return ((a <= b)&&(b < c)) || ((c < a)&&(a <= b)) || ((b < c)&&(c < a));
}


//����CRC-32 У��ͣ������͵������
static void put_frame(unsigned char *frame, int len) 
{
    *(unsigned int *)(frame + len) = crc32(frame, len);    //���ɲ����32λУ���
	send_frame(frame, len + 4);                            //���ڴ�frame�������鷢�͵������
	                                                       //ÿ�ֽڷ�����Ҫ1ms��֡��֮֡��ı߽籣��1ms
    phl_ready = 0;                                         //��������δ����
}


//��������֡
static void send_data(seq_nr frame_nr,seq_nr frame_expected,packet buffer[])
{
    frame s;

    s.seq = frame_nr;                                      //֡���
    s.ack = (frame_expected+ MAX_SEQ)%(MAX_SEQ+1);         //ȷ�Ϻ�
	memcpy(s.data.info, buffer[frame_nr].info, PKT_LEN);   //������������

	dbg_frame("==== Send DATA seq:%d ack:%d, ID %d\n", s.seq, s.ack, *(short *)s.data.info);

	put_frame((unsigned char *)&s, 2 + PKT_LEN);           //����У��Ͳ����͵�����㣬����=kind+ack+seq+data
	start_timer(frame_nr, DATA_TIMER);                     //����frame_nr�Ŷ�ʱ����ʱ��ΪDATA_TIMER
}

int main(int argc, char **argv)
{
	int event, arg;
    frame f;
    int len;

	seq_nr ack_expected;           //���ʹ����½�
	seq_nr next_frame_to_send;     //���ʹ����Ͻ�+1
	seq_nr frame_expected;         //���մ����½�
	
	packet buffer[MAX_SEQ+1];      //������
	seq_nr nbuffered;              //��������ʹ������
	seq_nr i;                      //�������±�

	/*��ʼ������*/
    protocol_init(argc, argv);     //Э�����л�����ʼ��
	enable_network_layer();       

	/*������ʼ��*/
	len =0;
	ack_expected = 0;
	next_frame_to_send = 0;
	frame_expected = 0;
	nbuffered = 0;

	lprintf("Designed by Zhang Zichao from Class 2011211303, build: " __DATE__"  "__TIME__"\n");

    while(true)
	{
        event = wait_for_event(&arg);                                   //�ȴ��¼�(��4�����)
        switch(event)
		{
            case NETWORK_LAYER_READY:                                   //���������¼�
				   nbuffered++;                                         //���ͻ�������ʹ��������1
                   get_packet(buffer[next_frame_to_send].info);         //��������ȡ����
                   send_data(next_frame_to_send,frame_expected,buffer); //��������
			       inc(next_frame_to_send);                             //next_frame_to_send��1
                   break;

            case PHYSICAL_LAYER_READY:                                  //���������¼�
                 phl_ready = 1;                                         //�����������
                 break;

            case FRAME_RECEIVED:                                        //֡�����¼�
                 len = recv_frame((unsigned char *)&f, sizeof(f));      //��������ȡ֡
                 if (len < 5 || crc32((unsigned char *)&f, len) !=0)    //CRCУ��ʹ���
				 {               
                     dbg_event("!!!! Receiver Error, Bad CRC Checksum\n");          
                     break;
                 }

				 /*��ȷ��������֡*/
				 dbg_frame("%%%% Recv DATA seq:%d ack:%d, ID %d\n", f.seq, f.ack, *(short *)f.data.info);;

				 if(f.seq == frame_expected)
				 {
					 put_packet(f.data.info,len-6);                     //�Ͻ����鵽����㣬����=֡����-ack-seq-CRC
					 inc(frame_expected);
				 }           

				 /*�����Ӵ�ȷ��ack*/
                 while(between(ack_expected,f.ack,next_frame_to_send))
				 {
                       nbuffered--;
                       stop_timer(ack_expected);                        //�رն�ʱ��
                       inc(ack_expected);                               //�޸ķ��ʹ��ڱ߽�
                 }
                 break;

            case DATA_TIMEOUT:                                          //��������֡δ�յ�ACK��ʱ�¼�
                 dbg_event(">>>> DATA %d timeout <<<<\n", arg);
		         next_frame_to_send = ack_expected;
				 for(i = 1; i <= nbuffered; i++)
				 {
					 send_data(next_frame_to_send,frame_expected,buffer); //�ط�����֡
					 inc(next_frame_to_send);
				 }
                 break;
        }

        if (nbuffered < MAX_SEQ && phl_ready)
            enable_network_layer();
        else
            disable_network_layer();
    }
}            
