#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"

#define MAX_SEQ    7                      //最大帧序号
#define DATA_TIMER 3000                   //DATA_TIMER时限            
#define inc(k) if(k<MAX_SEQ)k++;else k=0  //循环加1

typedef enum {false, true} bool;          //布尔类型
typedef unsigned char seq_nr;             //帧序号或确认号

typedef struct 
{
	unsigned char info[PKT_LEN];
}packet;                                  //分组

static int phl_ready = 0;                 //物理层就绪标记位

typedef struct
{
    seq_nr ack;                           //确认号
	seq_nr seq;                           //帧序号
    packet data;                          //网络层分组
    unsigned int  padding;                //CRC校验和
}frame;


//当帧到达时，判断该帧是否落在接收方窗口内
static bool between (seq_nr a, seq_nr b, seq_nr c)
{
	return ((a <= b)&&(b < c)) || ((c < a)&&(a <= b)) || ((b < c)&&(c < a));
}


//生成CRC-32 校验和，并发送到物理层
static void put_frame(unsigned char *frame, int len) 
{
    *(unsigned int *)(frame + len) = crc32(frame, len);    //生成并添加32位校验和
	send_frame(frame, len + 4);                            //将内存frame缓冲区块发送到物理层
	                                                       //每字节发送需要1ms，帧与帧之间的边界保留1ms
    phl_ready = 0;                                         //标记物理层未就绪
}


//发送数据帧
static void send_data(seq_nr frame_nr,seq_nr frame_expected,packet buffer[])
{
    frame s;

    s.seq = frame_nr;                                      //帧序号
    s.ack = (frame_expected+ MAX_SEQ)%(MAX_SEQ+1);         //确认号
	memcpy(s.data.info, buffer[frame_nr].info, PKT_LEN);   //拷贝网络层分组

	dbg_frame("==== Send DATA seq:%d ack:%d, ID %d\n", s.seq, s.ack, *(short *)s.data.info);

	put_frame((unsigned char *)&s, 2 + PKT_LEN);           //生成校验和并发送到物理层，长度=kind+ack+seq+data
	start_timer(frame_nr, DATA_TIMER);                     //启动frame_nr号定时器，时限为DATA_TIMER
}

int main(int argc, char **argv)
{
	int event, arg;
    frame f;
    int len;

	seq_nr ack_expected;           //发送窗口下界
	seq_nr next_frame_to_send;     //发送窗口上界+1
	seq_nr frame_expected;         //接收窗口下界
	
	packet buffer[MAX_SEQ+1];      //缓冲区
	seq_nr nbuffered;              //缓冲区已使用数量
	seq_nr i;                      //缓冲区下标

	/*初始化操作*/
    protocol_init(argc, argv);     //协议运行环境初始化
	enable_network_layer();       

	/*变量初始化*/
	len =0;
	ack_expected = 0;
	next_frame_to_send = 0;
	frame_expected = 0;
	nbuffered = 0;

	lprintf("Designed by Zhang Zichao from Class 2011211303, build: " __DATE__"  "__TIME__"\n");

    while(true)
	{
        event = wait_for_event(&arg);                                   //等待事件(共4种情况)
        switch(event)
		{
            case NETWORK_LAYER_READY:                                   //网络层就绪事件
				   nbuffered++;                                         //发送缓冲区已使用数量加1
                   get_packet(buffer[next_frame_to_send].info);         //从网络层获取分组
                   send_data(next_frame_to_send,frame_expected,buffer); //发送数据
			       inc(next_frame_to_send);                             //next_frame_to_send加1
                   break;

            case PHYSICAL_LAYER_READY:                                  //物理层就绪事件
                 phl_ready = 1;                                         //标记物理层就绪
                 break;

            case FRAME_RECEIVED:                                        //帧到达事件
                 len = recv_frame((unsigned char *)&f, sizeof(f));      //从物理层获取帧
                 if (len < 5 || crc32((unsigned char *)&f, len) !=0)    //CRC校验和错误
				 {               
                     dbg_event("!!!! Receiver Error, Bad CRC Checksum\n");          
                     break;
                 }

				 /*正确接收数据帧*/
				 dbg_frame("%%%% Recv DATA seq:%d ack:%d, ID %d\n", f.seq, f.ack, *(short *)f.data.info);;

				 if(f.seq == frame_expected)
				 {
					 put_packet(f.data.info,len-6);                     //上交分组到网络层，长度=帧长度-ack-seq-CRC
					 inc(frame_expected);
				 }           

				 /*处理捎带确认ack*/
                 while(between(ack_expected,f.ack,next_frame_to_send))
				 {
                       nbuffered--;
                       stop_timer(ack_expected);                        //关闭定时器
                       inc(ack_expected);                               //修改发送窗口边界
                 }
                 break;

            case DATA_TIMEOUT:                                          //发送数据帧未收到ACK超时事件
                 dbg_event(">>>> DATA %d timeout <<<<\n", arg);
		         next_frame_to_send = ack_expected;
				 for(i = 1; i <= nbuffered; i++)
				 {
					 send_data(next_frame_to_send,frame_expected,buffer); //重发数据帧
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
