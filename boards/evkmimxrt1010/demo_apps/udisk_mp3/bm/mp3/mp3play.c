#include "mp3play.h"
#include "mp3_config.h"
#include "ff.h"
#include "string.h"
#include "FSL_DEBUG_CONSOLE.h"

#define printf PRINTF
#define AUDIO_MIN(x,y)	((x)<(y)? (x):(y))

//////////////////////////////////////////////////////////////////////////////////	 
//��������ֲ��helix MP3�����
//ALIENTEK STM32F407������
//MP3 �������	   
//����ԭ��@ALIENTEK
//������̳:www.openedv.com
//��������:2014/6/29
//�汾��V1.0
//********************************************************************************
//V1.0 ˵��
//1,֧��16λ������/������MP3�Ľ���
//2,֧��CBR/VBR��ʽMP3����
//3,֧��ID3V1��ID3V2��ǩ����
//4,֧�����б�����(MP3�����320Kbps)����
////////////////////////////////////////////////////////////////////////////////// 	
 

FIL f_wave;
void close_wave_file(void)
{
    f_close(&f_wave);
}
void open_wave_file(void)
{
    FRESULT error;
    error = f_open(&f_wave, _T(PCM_FILEPATH), (FA_WRITE | FA_CREATE_ALWAYS));
    if (error)
    {
        printf("open decoded pcm file fail.\r\n");
        while(1);
    }    
    else
    {
        printf("open decoded pcm file done.\r\n");
    }
}
void mp3_fill_buffer(u16* buf,u16 sample_cnt,u8 nch)
{
    // send to SAI
#if 1  
    void SAI_send_audio(char * buf, uint32_t size);
    //SAI_send_audio(buf, sample_cnt*nch);
#endif

#if 0
    // write to files
    static int size_total = 0;
    size_total += sample_cnt*nch;
	PRINTF("get pcm data: %d, sample_cnt: %d\r\n", size_total, sample_cnt);
    UINT bytesWritten;
    f_write(&f_wave, buf, sample_cnt*nch, &bytesWritten);
#endif
}


//����ID3V1 
//buf:�������ݻ�����(��С�̶���128�ֽ�)
//pctrl:MP3������
//����ֵ:0,��ȡ����
//    ����,��ȡʧ��
u8 mp3_id3v1_decode(u8* buf,__mp3ctrl *pctrl)
{
	ID3V1_Tag *id3v1tag;
	id3v1tag=(ID3V1_Tag*)buf;
	if (strncmp("TAG",(char*)id3v1tag->id,3)==0)//��MP3 ID3V1 TAG
	{
		if(id3v1tag->title[0])strncpy((char*)pctrl->title,(char*)id3v1tag->title,30);
		if(id3v1tag->artist[0])strncpy((char*)pctrl->artist,(char*)id3v1tag->artist,30); 
	}else return 1;
	return 0;
}
//����ID3V2 
//buf:�������ݻ�����
//size:���ݴ�С
//pctrl:MP3������
//����ֵ:0,��ȡ����
//    ����,��ȡʧ��
u8 mp3_id3v2_decode(u8* buf,u32 size,__mp3ctrl *pctrl)
{
	ID3V2_TagHead *taghead;
	ID3V23_FrameHead *framehead; 
	u32 t;
	u32 tagsize;	//tag��С
	u32 frame_size;	//֡��С 
	taghead=(ID3V2_TagHead*)buf; 
	if(strncmp("ID3",(const char*)taghead->id,3)==0)//����ID3?
	{
		tagsize=((u32)taghead->size[0]<<21)|((u32)taghead->size[1]<<14)|((u16)taghead->size[2]<<7)|taghead->size[3];//�õ�tag ��С
		pctrl->datastart=tagsize;		//�õ�mp3���ݿ�ʼ��ƫ����
		if(tagsize>size)tagsize=size;	//tagsize��������bufsize��ʱ��,ֻ��������size��С������
		if(taghead->mversion<3)
		{
			printf("not supported mversion!\r\n");
			return 1;
		}
		t=10;
		while(t<tagsize)
		{
			framehead=(ID3V23_FrameHead*)(buf+t);
			frame_size=((u32)framehead->size[0]<<24)|((u32)framehead->size[1]<<16)|((u32)framehead->size[2]<<8)|framehead->size[3];//�õ�֡��С
 			if (strncmp("TT2",(char*)framehead->id,3)==0||strncmp("TIT2",(char*)framehead->id,4)==0)//�ҵ���������֡,��֧��unicode��ʽ!!
			{
				strncpy((char*)pctrl->title,(char*)(buf+t+sizeof(ID3V23_FrameHead)+1),AUDIO_MIN(frame_size-1,MP3_TITSIZE_MAX-1));
			}
 			if (strncmp("TP1",(char*)framehead->id,3)==0||strncmp("TPE1",(char*)framehead->id,4)==0)//�ҵ�����������֡
			{
				strncpy((char*)pctrl->artist,(char*)(buf+t+sizeof(ID3V23_FrameHead)+1),AUDIO_MIN(frame_size-1,MP3_ARTSIZE_MAX-1));
			}
			t+=frame_size+sizeof(ID3V23_FrameHead);
		} 
	}
    else 
        pctrl->datastart=0;//������ID3,mp3�����Ǵ�0��ʼ
	return 0;
} 

//��ȡMP3������Ϣ
//pname:MP3�ļ�·��
//pctrl:MP3������Ϣ�ṹ�� 
//����ֵ:0,�ɹ�
//    ����,ʧ��
u8 mp3_get_info(u8 *pname,__mp3ctrl* pctrl)
{
    HMP3Decoder decoder;
    MP3FrameInfo frame_info;
	MP3_FrameXing* fxing;
	MP3_FrameVBRI* fvbri;
	FIL*fmp3;
	u8 *buf;
	u32 br;
	u8 res;
	int offset=0;
	u32 p;
	short samples_per_frame;	//һ֡�Ĳ�������
	u32 totframes;				//��֡��
	
    FIL f;
	fmp3=&f; //mymalloc(SRAMIN,sizeof(FIL)); 
    
    u8 buf_mp3[5*1024];
	buf = buf_mp3; //mymalloc(SRAMIN,5*1024);		//����5K�ڴ� 

	if(fmp3&&buf)//�ڴ�����ɹ�
	{ 		
		res = f_open(fmp3,(MP3_FILEPATH),FA_READ);//���ļ�
		res=f_read(fmp3,(char*)buf,5*1024,&br);
		if(res==0)//��ȡ�ļ��ɹ�,��ʼ����ID3V2/ID3V1�Լ���ȡMP3��Ϣ
		{  
			mp3_id3v2_decode(buf,br,pctrl);	//����ID3V2����
			//f_lseek(fmp3,fmp3->fsize-128);	//ƫ�Ƶ�����128��λ��
            f_lseek(fmp3,f_size(fmp3)-128);	//ƫ�Ƶ�����128��λ��
            
			f_read(fmp3,(char*)buf,128,&br);//��ȡ128�ֽ�
			mp3_id3v1_decode(buf,pctrl);	//����ID3V1����  
			decoder=MP3InitDecoder(); 		//MP3���������ڴ�
			f_lseek(fmp3,pctrl->datastart);	//ƫ�Ƶ����ݿ�ʼ�ĵط�
			f_read(fmp3,(char*)buf,5*1024,&br);	//��ȡ5K�ֽ�mp3����
 			offset=MP3FindSyncWord(buf,br);	//����֡ͬ����Ϣ
			if(offset>=0&&MP3GetNextFrameInfo(decoder,&frame_info,&buf[offset])==0)//�ҵ�֡ͬ����Ϣ��,����һ����Ϣ��ȡ����	
			{ 
				p=offset+4+32;
				fvbri=(MP3_FrameVBRI*)(buf+p);
				if(strncmp("VBRI",(char*)fvbri->id,4)==0)//����VBRI֡(VBR��ʽ)
				{
					if (frame_info.version==MPEG1)samples_per_frame=1152;//MPEG1,layer3ÿ֡����������1152
					else samples_per_frame=576;//MPEG2/MPEG2.5,layer3ÿ֡����������576 
 					totframes=((u32)fvbri->frames[0]<<24)|((u32)fvbri->frames[1]<<16)|((u16)fvbri->frames[2]<<8)|fvbri->frames[3];//�õ���֡��
					pctrl->totsec=totframes*samples_per_frame/frame_info.samprate;//�õ��ļ��ܳ���
				}else	//����VBRI֡,�����ǲ���Xing֡(VBR��ʽ)
				{  
					if (frame_info.version==MPEG1)	//MPEG1 
					{
						p=frame_info.nChans==2?32:17;
						samples_per_frame = 1152;	//MPEG1,layer3ÿ֡����������1152
					}else
					{
						p=frame_info.nChans==2?17:9;
						samples_per_frame=576;		//MPEG2/MPEG2.5,layer3ÿ֡����������576
					}
					p+=offset+4;
					fxing=(MP3_FrameXing*)(buf+p);
					if(strncmp("Xing",(char*)fxing->id,4)==0||strncmp("Info",(char*)fxing->id,4)==0)//��Xng֡
					{
						if(fxing->flags[3]&0X01)//������frame�ֶ�
						{
							totframes=((u32)fxing->frames[0]<<24)|((u32)fxing->frames[1]<<16)|((u16)fxing->frames[2]<<8)|fxing->frames[3];//�õ���֡��
							pctrl->totsec=totframes*samples_per_frame/frame_info.samprate;//�õ��ļ��ܳ���
						}
                        else	//��������frames�ֶ�
						{
							//pctrl->totsec=fmp3->fsize/(frame_info.bitrate/8);
                            pctrl->totsec=f_size(fmp3)/(frame_info.bitrate/8);
						} 
					}
                    else 		//CBR��ʽ,ֱ�Ӽ����ܲ���ʱ��
					{
						//pctrl->totsec=fmp3->fsize/(frame_info.bitrate/8);
                        pctrl->totsec = f_size(fmp3)/(frame_info.bitrate/8);
					}
				} 
				pctrl->bitrate=frame_info.bitrate;			//�õ���ǰ֡������
				pctrl->samplerate=frame_info.samprate; 	//�õ�������. 
				if(frame_info.nChans==2)pctrl->outsamples=frame_info.outputSamps; //���PCM��������С 
				else pctrl->outsamples=frame_info.outputSamps*2; //���PCM��������С,���ڵ�����MP3,ֱ��*2,����Ϊ˫�������
			}else res=0XFE;//δ�ҵ�ͬ��֡	
			MP3FreeDecoder(decoder);//�ͷ��ڴ�		
		} 
		f_close(fmp3);
	}
    else 
        res=0XFF;
    
	return res;	
}  






FIL audioFile;

#include "gpt.h"
static u8* readptr;	//MP3�����ָ��
static int offset=0;	//ƫ����
static int bytesleft=0;//buffer��ʣ�����Ч����
u8 mp3_buf[MP3_FILE_BUF_SZ];
//u8 buft[2304*2];
HMP3Decoder mp3decoder;
__mp3ctrl my_mp3_ctrl;

#define DECODE_END 0
#define DECODE_OK  1

u8 mp3_decode_one_frame(u8 * buf_out)
{
    u8 res; 
    u32 br=0; 
    int err=0; 
    MP3FrameInfo mp3frameinfo;
    
    // PRINTF("mp3_decode_one_frame");

    offset=MP3FindSyncWord(readptr,bytesleft);//��readptrλ��,��ʼ����ͬ���ַ�
    if(offset<0)
    { 
        //û���ҵ�ͬ���ַ�������load���ݿ�
        readptr=mp3_buf;	// MP3��ָ��ָ��buffer
        offset=0;		    // ƫ����Ϊ0
        bytesleft=0;
        
        res=f_read(&audioFile,mp3_buf,MP3_FILE_BUF_SZ,&br);//һ�ζ�ȡMP3_FILE_BUF_SZ�ֽ�
        if(res)  //�����ݳ�����
        {
            return DECODE_END;
        }
        if(br==0) //����Ϊ0,˵�����������.
        {
            return DECODE_END;
        }

        bytesleft+=br;	//buffer�����ж�����ЧMP3����?
        err=0;                  
    }
    else	
    {
        //�ҵ�ͬ���ַ���
        readptr+=offset;		//MP3��ָ��ƫ�Ƶ�ͬ���ַ���.
        bytesleft-=offset;		//buffer�������Ч���ݸ���,�����ȥƫ����
        
        //gp_timer_measure_begin();
        err=MP3Decode(mp3decoder,&readptr,&bytesleft,(short*)buf_out,0);//����һ֡MP3����
        //int us = gp_timer_measure_end();
        
        // PRINTF("CPU loading: %d\r\n", us/(2304/48000));
        // PRINTF("CPU loading: %d\r\n", us*48000*100/(2304*1000*1000) );
        // PRINTF("CPU loading: %d\r\n", us*48*100/(2304*1000) );

        if(err!=0)
        {
            printf("decode error:%d\r\n",err);
            return DECODE_END;
        }
        else
        {
            MP3GetLastFrameInfo(mp3decoder,&mp3frameinfo);	//�õ��ոս����MP3֡��Ϣ
            if(my_mp3_ctrl.bitrate!=mp3frameinfo.bitrate)	//��������
            {
                my_mp3_ctrl.bitrate = mp3frameinfo.bitrate;
            }
            
            
            // ********************************************
            // fill decoded buffer.
            // ********************************************                        
            //mp3_fill_buffer((u16*)buft,mp3frameinfo.outputSamps,mp3frameinfo.nChans);//���pcm���� 
        }
        
        
        // read new data
        if(bytesleft<MAINBUF_SIZE*2)//����������С��2��MAINBUF_SIZE��ʱ��,���벹���µ����ݽ���.
        { 
            memmove(mp3_buf,readptr,bytesleft);//�ƶ�readptr��ָ������ݵ�buffer����,��������СΪ:bytesleft
            f_read(&audioFile,mp3_buf+bytesleft,MP3_FILE_BUF_SZ-bytesleft,&br);//�������µ�����
            if(br<MP3_FILE_BUF_SZ-bytesleft)
            {
                memset(mp3_buf+bytesleft+br,0,MP3_FILE_BUF_SZ-bytesleft-br); 
            }
            bytesleft=MP3_FILE_BUF_SZ;  
            readptr=mp3_buf; 
        }
    }
    
    return DECODE_OK;
}

void mp3_play_clean(void);

u8 mp3_play_song(u8* fname)
{ 

	u8 res;
	u32 br=0; 
    
	memset(&my_mp3_ctrl,0,sizeof(__mp3ctrl));//�������� 
        open_wave_file();
	res=mp3_get_info(fname,&my_mp3_ctrl);  
	if(res==0)
	{ 
		printf("     title:%s\r\n",   my_mp3_ctrl.title); 
		printf("    artist:%s\r\n",   my_mp3_ctrl.artist); 
		printf("   bitrate:%dbps\r\n",my_mp3_ctrl.bitrate);	
		printf("samplerate:%d\r\n",   my_mp3_ctrl.samplerate);	
		printf("  totalsec:%d\r\n",   my_mp3_ctrl.totsec); 		
		mp3decoder=MP3InitDecoder(); 					//MP3���������ڴ�
		res=f_open(&audioFile,(MP3_FILEPATH),FA_READ);	//���ļ�
	}
    else
    {
        printf("get mp3 information error\r\n");
        while(1)
            ;
    }
	if(res==0&&mp3decoder!=0)//���ļ��ɹ�
	{ 
		f_lseek(&audioFile,my_mp3_ctrl.datastart);	//�����ļ�ͷ��tag��Ϣ							//��ʼ���� 
		//while(1) 
		{
          
            // *** now begin to decode MP3 file ***
          
			readptr=mp3_buf;	// MP3��ָ��ָ��buffer
			offset=0;		    // ƫ����Ϊ0
			bytesleft=0;
            
			res=f_read(&audioFile,mp3_buf,MP3_FILE_BUF_SZ,&br);//һ�ζ�ȡMP3_FILE_BUF_SZ�ֽ�
			if(res)  //�����ݳ�����
			{
				return 0;
			}
			if(br==0) //����Ϊ0,˵�����������.
			{
				return 0;
			}

			bytesleft+=br;	//buffer�����ж�����ЧMP3����?

/*            
			while(1)//û�г��������쳣(���ɷ��ҵ�֡ͬ���ַ�)
			{
                if( mp3_decode_one_frame() == DECODE_END)
                {
                    mp3_play_clean();
                    break;
                }
			}  
*/
		}
	}
    return 0;
}


void mp3_play_clean(void)
{
	f_close(&audioFile);
    close_wave_file();
	MP3FreeDecoder(mp3decoder);		//�ͷ��ڴ�	
}

















