#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <string.h>
#include <sys/mman.h>
#include "jpeglib.h"
#include <linux/fb.h>
#include <linux/input.h>
#include <pthread.h>
#include "tslib.h"
#include <stdlib.h>
#include <dirent.h>
#include <semaphore.h>
#include <sys/time.h>
#include "libyuv.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
int fd_fb;
int screen_size;//屏幕像素大小
int LCD_width;//LCD宽度
int LCD_height;//LCD高度

//int pressure = 0;

u16 *fbbase = NULL;//LCD显存地址
unsigned long line_length;       //LCD一行的长度（字节为单位）
unsigned int bpp;    //像素深度bpp


int fd;
int fd_v4l2;
int read_x=0, read_y=0;
int start_read_flag = 1;
int start_xiangce_flag = 1;

struct tsdev *ts = NULL;
int image_count = 0;
const char *background1 = "/root/background/background1.jpg";
const char *background2 = "/root/background/background2.jpg";

struct jpeg_node *image_list = NULL;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
//pthread_mutex_t mutex2 = PTHREAD_MUTEX_INITIALIZER;
//pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
//sem_t sem_id;

u16 rgb888_to_rgb565(u8 r, u8 g, u8 b) {
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}


struct jpeg_node{
	char name[30];				//图像名
	struct jpeg_node *next; //下一张
	struct jpeg_node *pre;	//上一张
};


//初始化链表
struct jpeg_node *jpeg_list_Init(void)
{
	struct jpeg_node* jpeg_head = malloc(sizeof(struct jpeg_node));
	strcpy(jpeg_head->name, background2);
	jpeg_head ->pre = jpeg_head;
	jpeg_head ->next = NULL;
	return jpeg_head;
}
//插入一个新节点
void jpeg_list_insert(struct jpeg_node *jpeg_head, char *name)
{
	struct jpeg_node *node = malloc(sizeof(struct jpeg_node));
	strcpy(node->name, name);
	if(!jpeg_head->next){
		jpeg_head->next=node;
		node->next=node;
		node->pre=node;
	}else{
		node->next=jpeg_head->next;
		node->pre=jpeg_head->next->pre;
		jpeg_head->next->pre->next=node;
		jpeg_head->next->pre=node;
		jpeg_head->next=node;
	}

}

//遍历链表
void jpeg_list_printf(void)
{
	struct jpeg_node* pnext= image_list->next,*first_node= pnext;
	while(pnext != NULL)
	{
		printf("   %s\n", pnext->name);
		pnext = pnext->next;
		if(pnext==first_node) break;
	}
}

//初始化触摸屏
int Touch_screen_Init(void)
{
	ts = ts_setup(NULL, 0);//以阻塞打开
	if(NULL == ts)
	{
		perror("触摸屏初始化失败");
	}
	return 0;
}

//初始化LCD
int LCD_Init(void)
{
	struct fb_var_screeninfo var;   /* Current var */
	struct fb_fix_screeninfo fix;   /* Current fix */
	fd_fb = open("/dev/fb0", O_RDWR);
	if(fd_fb < 0)
	{
		perror("打开LCD失败");
		return -1;
	}
	//获取LCD信息
	ioctl(fd_fb, FBIOGET_VSCREENINFO, &var);//获取屏幕可变信息
	ioctl(fd_fb, FBIOGET_FSCREENINFO, &fix);//获取屏幕固定信息
	//LCD_width  = var.xres * var.bits_per_pixel / 8;
    //pixel_width = var.bits_per_pixel / 8;
    screen_size = var.xres * var.yres * var.bits_per_pixel / 8;
	LCD_width = var.xres;
	LCD_height = var.yres;
	bpp = var.bits_per_pixel;
	line_length = fix.line_length;
	printf("LCD分辨率：%d %d\n",LCD_width, LCD_height);
	printf("bpp: %d\n", bpp);
	printf("bpp: %ld\n", line_length);
	fbbase = (u16*)mmap(NULL, screen_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_fb, 0);//映射
	close(fd_fb);
	if (fbbase == (u16 *)-1)
    {
        printf("can't mmap\n");
        return -1;
    }
    memset(fbbase, 0xFF, screen_size);//LCD设置为白色背景
    
    return 0;
}


int read_touchscreen(int *x, int *y)
{
	struct ts_sample samp;
	//struct timeval tPreTime;
	static int is_pressed=0;
	while(1){
		if (ts_read(ts, &samp, 1) < 0) 
		{
			perror("ts_read error");
			ts_close(ts);
			return -1;
		}
		if(samp.pressure > 0 && !is_pressed){
			*x = samp.x;
			*y = samp.y;
			is_pressed = 1;
			printf("anxia : %d %d", samp.x, samp.y);
			break;
		}else if (samp.pressure == 0 && is_pressed) {
			is_pressed = 0;
			continue;
		}
	}
	return 0;
}
	

int LCD_JPEG_Show(const char *JpegData, int size)
{
	int min_hight = LCD_height, min_width = LCD_width, valid_bytes;
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
	cinfo.err = jpeg_std_error(&jerr);

	jpeg_create_decompress(&cinfo);
	//指定解码数据源
	jpeg_mem_src(&cinfo, JpegData, size);
	//读取图像信息
	jpeg_read_header(&cinfo, TRUE);
	//printf("jpeg图像的大小为：%d*%d\n", cinfo.image_width, cinfo.image_height);
	//设置解码参数
	cinfo.out_color_space = JCS_RGB;//可以不设置默认为RGB
	//cinfo.scale_num = 1;
	//cinfo.scale_denom = 1;设置图像缩放，scale_num/scale_denom缩放比例，默认为1
	//开始解码
	jpeg_start_decompress(&cinfo);
	
	//为缓冲区分配空间
	unsigned char*jpeg_line_buf = malloc(cinfo.output_components * cinfo.output_width);
	unsigned int*fb_line_buf = malloc(line_length);//每个成员4个字节和RGB888对应
	//判断图像和LCD屏那个分辨率更低
	if(cinfo.output_width < min_width)
		min_width = cinfo.output_width;
	if(cinfo.output_height < min_hight)
		min_hight = cinfo.output_height;
	//读取数据，数据按行读取
	valid_bytes = min_width * bpp / 8;//一行的有效字节数，实际写进LCD显存的一行数据大小
	u16*ptr = fbbase;
	while(cinfo.output_scanline < min_hight)
	{
		jpeg_read_scanlines(&cinfo, &jpeg_line_buf, 1);//每次读取一行
		//将读取到的BGR888数据转化为RGB888
		unsigned int red, green, blue;
		unsigned int color;  
		for(int i = 0; i < min_width; i++)
		{
			red = jpeg_line_buf[i*3];
			green = jpeg_line_buf[i*3+1];
			blue = jpeg_line_buf[i*3+2];
			color = red<<16 | green << 8 | blue;
			fb_line_buf[i] = color;
		}
		memcpy(ptr, fb_line_buf, valid_bytes);
		ptr += LCD_width*bpp/8;
	}
	//完成解码
	jpeg_finish_decompress(&cinfo);
	//销毁解码对象
	jpeg_destroy_decompress(&cinfo);
	//释放内存
	free(jpeg_line_buf);
	free(fb_line_buf);
	return 1;
}

//指定JPEG文件显示在LCD上,传入jpeg文件路径
int LCD_Show_JPEG(const char *jpeg_path)
{
	FILE *jpeg_file = NULL;
	jpeg_file = fopen(jpeg_path, "r");	//只读方式打开
	if(jpeg_file==NULL){
		printf("文件路径错误\n");
		return -1;
	}
	int  valid_bytes;
	valid_bytes = LCD_width * bpp / 8;//一行的有效字节数，实际写进LCD显存的一行数据大小

	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
	JSAMPARRAY buffer;
    int row_stride;
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_decompress(&cinfo);

	jpeg_stdio_src(&cinfo, jpeg_file);

	jpeg_read_header(&cinfo, TRUE);


	cinfo.out_color_space = JCS_RGB;//可以不设置默认为RGB


	jpeg_start_decompress(&cinfo);

	//为缓冲区分配空间
	row_stride = cinfo.output_width * cinfo.output_components;
	buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);
	
	u16*fb_line_buf = (u16*)malloc(valid_bytes);
	if(!buffer  || !fb_line_buf){
		printf("内存分配失败\n");
		return -1;
	}

	u16 *ptr = fbbase;
	while(cinfo.output_scanline < cinfo.output_height)
	{
		jpeg_read_scanlines(&cinfo, buffer, 1);//每次读取一行
		//将读取到的RGB888数据转化为RGB565
		u8 r,g,b;
		for(int i = 0; i < cinfo.output_width; i++)
		{
			r = buffer[0][i*3];
			g = buffer[0][i*3+1];
			b = buffer[0][i*3+2];
			fb_line_buf[i] = rgb888_to_rgb565(r,g,b);
		}
		memcpy(ptr, fb_line_buf, cinfo.output_width*2);
		ptr += LCD_width;
	}
	//完成解码
	jpeg_finish_decompress(&cinfo);;
	//销毁解码对象
	jpeg_destroy_decompress(&cinfo);
	free(fb_line_buf);
	return 1;
}

//把照片放入相册，image_count为目前照片名称最大索引
int xiangce_Init(void)
{
	image_list = jpeg_list_Init();
	DIR *dp = opendir("/root/picture");	//打开home目录
	if(dp==NULL){
		return -1;
	}

	struct dirent *pdir;
	char *temp = NULL;
	char name[20];
	//char newname[20];
	int total = 0;
	//遍历目录， 当遍历结束时返回NULL
	while(pdir = readdir(dp))	
	{
		if(pdir->d_type == DT_REG)				//判断是否为普通文件
		{
			if(strstr(pdir->d_name, ".jpg"))	//判断是否为jpg文件
			{
				char newname[64] = {0};
				sprintf(newname,"/root/picture/%s", pdir->d_name);
				jpeg_list_insert(image_list, newname);//将该文件名称插入链表中
				bzero(name,20);
				strcpy(name, pdir->d_name);
				temp = strtok(name, ".");
				total = atoi(temp) > total ? atoi(temp) : total;
			}
		}
	}
	temp = NULL;
	return total;
}
//线程函数，开始读取屏幕坐标

void *start_read(void *arg)
{
	int x = 0, y =0;
	while(start_read_flag)
	{
		read_touchscreen(&x, &y);
		if(x  > 400 && x < 480 && y > 0 && y < 272)
		{
			pthread_mutex_lock(&mutex);
			read_x = x;
 			read_y = y;
			pthread_mutex_unlock(&mutex);
		}
		printf("  readx = %d, ready = %d\n", read_x, read_y);
		
	}
	return NULL;
}

//打开相册
void start_xiangce(void)
{
	
		struct jpeg_node *curr_image = image_list->next; //->next;//指向第一张图片    
		LCD_Show_JPEG(background2);
		if(curr_image) LCD_Show_JPEG(curr_image->name);
		while(1)
		{
			if(curr_image){
				if(read_x>410 && read_x<450 && read_y>130 &&read_y<170)	//下一张
				{
					printf("下一张\n");
					curr_image = curr_image->next;
					LCD_Show_JPEG(curr_image->name);
					pthread_mutex_lock(&mutex);
					read_x = 0;
					read_y = 0;
					pthread_mutex_unlock(&mutex);
					printf("current image name :%s\n", curr_image->name);
				}
				if(read_x>410 && read_x<450 && read_y>0 && read_y<60)	//上一张
				{
					curr_image = curr_image->pre;
					LCD_Show_JPEG(curr_image->name);
					pthread_mutex_lock(&mutex);
					read_x = 0;
					read_y = 0;
					pthread_mutex_unlock(&mutex);
					printf("current image name :%s\n", curr_image->name);

				}
			}
			if(read_x>410 && read_x<450 && read_y>260 && read_y<272)	//返回
			{
				LCD_Show_JPEG(background1);
				printf("返回\n");
				break;
			}
		}
}

void YUY2ToRGB565(const u8* v4l_src, u8*argb_dst,u8* rgb565_dst,int video_width,int video_height){
	YUY2ToARGB(v4l_src,video_width*2,argb_dst,video_width*4,video_width,video_height);
	ARGBToRGB565(argb_dst,video_width*4,rgb565_dst,video_width*2,video_width,video_height);
}
void LCD_show(u16* rgb565_data,int width,int height){
	u16 *lcd_mem=fbbase;
	u16 *data=rgb565_data;
	for(int i=0;i<height;i++){
		memcpy(lcd_mem,data,width*2);
		lcd_mem+=LCD_width;
		data+=width;
	}
}


void compress_image_to_jpeg(const char *filename, int width, int height,u8 *image_buffer) {
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE *outfile;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    outfile = fopen(filename, "w+");
    if (!outfile) {
        fprintf(stderr, "Can't open %s\n", filename);
        exit(1);
    }
    jpeg_stdio_dest(&cinfo, outfile);

    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3; // RGB
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, 75, TRUE);


    jpeg_start_compress(&cinfo, TRUE);

    JSAMPROW row_pointer[1];
    int row_stride = width * 3;

    while (cinfo.next_scanline < cinfo.image_height) {
        row_pointer[0] = &image_buffer[cinfo.next_scanline * row_stride];
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    fclose(outfile);
}

int main(void)
{
	int ret=-1;
	Touch_screen_Init();

	LCD_Init();

	if(LCD_Show_JPEG(background1)<0) {
		printf("LCD_Show_JPEG初始化失败\n");
		exit(-1);
	}
	int fd = open("/dev/video1", O_RDWR);
	if(fd < 0)
	{
		perror("打开设备失败\n");
		exit(-1);
	}

	image_count = xiangce_Init();
	printf("相册初始化成功\n");
	if(image_count<0) printf("相册初始化失败\n");
	printf("Reading album...\n");
	jpeg_list_printf(); //


	
	struct v4l2_capability cap;
	if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
        perror("VIDIOC_QUERYCAP\n");
        close(fd);
        return 1;
    }
	printf("Driver: %s\n", cap.driver);
    printf("Card: %s\n", cap.card);
    printf("Bus info: %s\n", cap.bus_info);
    printf("Version: %u.%u.%u\n",
           (cap.version >> 16) & 0xFF,
           (cap.version >> 8) & 0xFF,
           cap.version & 0xFF);
    if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)
        printf("Device supports video capture.\n");

    if (cap.capabilities & V4L2_CAP_STREAMING)
        printf("Device supports streaming I/O.\n");

	//1.枚举摄像头支持的采集格式和分辨率
	struct v4l2_fmtdesc fmtdesc;
	fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmtdesc.index=0;
	printf("Supported video formats:\n");
	while(ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc)==0){
		 printf("   %s (FourCC: %c%c%c%c)\n",
           fmtdesc.description,
           fmtdesc.pixelformat & 0xFF,
           (fmtdesc.pixelformat >> 8) & 0xFF,
           (fmtdesc.pixelformat >> 16) & 0xFF,
           (fmtdesc.pixelformat >> 24) & 0xFF);
		struct v4l2_frmsizeenum fsize;
		fsize.index=0;
		fsize.pixel_format=fmtdesc.pixelformat;
		printf("	 ");
		while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fsize) == 0) {
			printf("%ux%u  ", fsize.discrete.width, fsize.discrete.height);
        	fsize.index++;
    	}
		printf("\n");
    	fmtdesc.index++;
	}

	//2.设置当前摄像头采集格式和分辨率
	struct v4l2_format vfmt;
	memset(&vfmt, 0, sizeof(vfmt));
	vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; // 设置为视频捕捉
	vfmt.fmt.pix.width = 320; // 设置图像宽度
	vfmt.fmt.pix.height = 240; // 设置图像高度
	vfmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV; //设置像素格式为YUYV422
	if (ioctl(fd, VIDIOC_S_FMT, &vfmt) < 0) {
        perror("Failed to set format");
        close(fd);
        return 1;
    }

	//3.获取当前摄像头采集格式和分辨率
	memset(&vfmt,0,sizeof(vfmt));
	vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(fd, VIDIOC_G_FMT, &vfmt) < 0) {
        perror("Failed to get format");
        close(fd);
        return 1;
    }
	printf("Current format:\n");
	printf("   Pixel Format: %c%c%c%c  %ux%u\n",
           vfmt.fmt.pix.pixelformat & 0xFF,
           (vfmt.fmt.pix.pixelformat >> 8) & 0xFF,
           (vfmt.fmt.pix.pixelformat >> 16) & 0xFF,
           (vfmt.fmt.pix.pixelformat >> 24) & 0xFF,
		   vfmt.fmt.pix.width,vfmt.fmt.pix.height);
	printf("   Bytes per Line: %u\n", vfmt.fmt.pix.bytesperline);
	printf("   Image Size: %u\n", vfmt.fmt.pix.sizeimage);
    printf("   Colorspace: %u\n", vfmt.fmt.pix.colorspace);

	int video_width=vfmt.fmt.pix.width;
	int video_height=vfmt.fmt.pix.height;

	//4.申请缓冲队列
	struct v4l2_requestbuffers reqbuffer;
	reqbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	reqbuffer.count = 4;	//申请4个缓冲区
	reqbuffer.memory = V4L2_MEMORY_MMAP;	//采用内存映射的方式
	printf("Requesting buffer...\n");
	if(ioctl(fd, VIDIOC_REQBUFS, &reqbuffer) < 0)
	{
		perror("Failed to request buffer");
		close(fd);
        return 1;
	}
	

	//映射，映射之前需要查询缓存信息->每个缓冲区逐个映射->将缓冲区放入队列
	struct v4l2_buffer mapbuffer;
	u8 *mmpaddr[4]; //用于存储映射后的首地址
	u32 addr_length[4]; //存储映射后空间的大小
	mapbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	printf("Mapping buffer...\n");
	for(int i = 0; i < 4; i++)
	{
		mapbuffer.index = i;
		if(ioctl(fd, VIDIOC_QUERYBUF, &mapbuffer)< 0){
			perror("Failed to request buffer");
			close(fd);
        	return 1;
		}	
		mmpaddr[i] = (u8*)mmap(NULL, mapbuffer.length, PROT_READ|PROT_WRITE, MAP_SHARED, fd, mapbuffer.m.offset);//mapbuffer.m.offset映射文件的偏移量
		addr_length[i] = mapbuffer.length;
		//放入队列
		if(ioctl(fd, VIDIOC_QBUF, &mapbuffer)< 0){
			perror("Failed to queue buffer");
			close(fd);
        	return 1;
		}
	}
	
	//打开设备
	//int read_x, read_y;
	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	printf("Streaming on...\n");
	if(ioctl(fd, VIDIOC_STREAMON, &type)< 0)
	{
		perror("Failed to stream on");
			close(fd);
        	return 1;
	}


	pthread_t pthread_read;
	pthread_create(&pthread_read, NULL, start_read, NULL);

	u8*argb_dst=malloc(video_height*video_width*4);
	u16* rgb565_dst=malloc(video_height*video_width*2);
	u8*rgb24_dst=malloc(video_height*video_width*3);
	struct v4l2_buffer readbuffer;
	readbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	

	while(1)
	{
		//从队列中提取一帧数据
		struct v4l2_buffer readbuffer;
		readbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	
		//出队列后得到缓存的索引index,得到对应缓存映射的地址mmpaddr[readbuffer.index]
		if(ioctl(fd, VIDIOC_DQBUF, &readbuffer) < 0){
			perror("获取数据失败");
			goto uvc_error;
			return 0;	
		}
			
		if(read_x > 400 && read_x < 480 && read_y > 0 && read_y < 272)
		{
			
			if(read_x > 400 && read_x < 480 && read_y > 20 && read_y < 100)
			{

				char newname[20] = {0};
				image_count++;
				sprintf(newname,"/root/picture/%d.jpg", image_count);
				ARGBToRGB24(argb_dst,video_width*4,rgb24_dst,video_width*3,video_width,video_height);
				compress_image_to_jpeg(newname,video_width,video_height,rgb24_dst);
				jpeg_list_insert(image_list, newname);
				printf("Snapping new image:%s\n", newname);
			}
			else if(read_x  > 400 && read_x < 480 && read_y > 180 && read_y < 220)
			{
				start_xiangce();				
			}
			pthread_mutex_lock(&mutex);
			read_x = 0;
			read_y = 0;
			pthread_mutex_unlock(&mutex);

		}
		YUY2ToRGB565(mmpaddr[readbuffer.index],argb_dst,rgb565_dst,video_width,video_height);
		LCD_show(rgb565_dst,video_width,video_height);
		//读取数据后将缓冲区放入队列
		if(ioctl(fd, VIDIOC_QBUF, &readbuffer)< 0){
			perror("放入队列失败");
			goto uvc_error;
			return 0;
		}
			
	}
	goto uvc_error;
	if(ioctl(fd, VIDIOC_STREAMOFF, &type) < 0)
		perror("关闭设备失败");
	return 0;

uvc_error:
	free(argb_dst);
	free(rgb565_dst);
	free(rgb24_dst);
	for(int i = 0; i < 4; i++)
		munmap(mmpaddr[i], addr_length[i]);
	start_read_flag = 0;	//结束线程
	start_xiangce_flag = 0;
	ts_close(ts);
	close(fd);
	close(fd_fb);
}



