/***********************************************************************************************
 * @file MQTTSN.c
 * @author dyl
 * @brief 
 * @version 0.1
 * @date 2022-12-04
 * 
 * @copyright Copyright (c) 2022
 * 
 * MQTTSN客户端，md就这一个文件用的vc远程连接的Linux，方便后续移植到嵌入式板子中。
 * 大部分代码还是 官方的packet包，只不过做了裁剪，对于官方的文档还是不太理解。
 * 足够理解的话可以实际的去修改不同的packet包。
 * 其实没啥修改的主要是pub包，或者过长字节的处理
 **********************************************************************************************/
/*头文件位置*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
/*宏定义位置*/
#define SOCKET_ERROR -1                          //套接字报错
#define INVALID_SOCKET SOCKET_ERROR
#define MQTTSNPacket_connectData_initializer { {'M', 'Q', 'S', 'C'}, 0, {NULL, {0, NULL}}, 10, 1, 0 } //连接时候的初始化

/*全局变量位置*/
typedef enum
{
	MQTTSN_TOPIC_TYPE_NORMAL, /* topic id in publish, topic name in subscribe */
	MQTTSN_TOPIC_TYPE_PREDEFINED,
	MQTTSN_TOPIC_TYPE_SHORT
}MQTTSN_topicTypes;


enum MQTTSN_msgTypes
{
    /*MQTTSN消息体类型*/
	MQTTSN_ADVERTISE, MQTTSN_SEARCHGW, MQTTSN_GWINFO, MQTTSN_RESERVED1, //网关，客户端不搞
	MQTTSN_CONNECT, MQTTSN_CONNACK,										//连接，客户端的。
	MQTTSN_WILLTOPICREQ, MQTTSN_WILLTOPIC, MQTTSN_WILLMSGREQ, MQTTSN_WILLMSG, //遗嘱暂时不了解。
	MQTTSN_REGISTER, MQTTSN_REGACK,									//应该也是遗嘱，不太了解，大概率不是。
	MQTTSN_PUBLISH, MQTTSN_PUBACK, MQTTSN_PUBCOMP, MQTTSN_PUBREC, MQTTSN_PUBREL, MQTTSN_RESERVED2,//pub
	MQTTSN_SUBSCRIBE, MQTTSN_SUBACK, MQTTSN_UNSUBSCRIBE, MQTTSN_UNSUBACK, //sub
	MQTTSN_PINGREQ, MQTTSN_PINGRESP,			//这个应该是心跳
	MQTTSN_DISCONNECT, MQTTSN_RESERVED3, 		//断开连接
	MQTTSN_WILLTOPICUPD, MQTTSN_WILLTOPICRESP, MQTTSN_WILLMSGUPD, MQTTSN_WILLMSGRESP, //特殊的遗嘱？
	MQTTSN_ENCAPSULATED = 0xfe					//保留位置
};

enum errors
{
    /*报错位置，暂时就这三吧。*/
	MQTTSNPACKET_BUFFER_TOO_SHORT = -2,
	MQTTSNPACKET_READ_ERROR = -1,
	MQTTSNPACKET_READ_COMPLETE
};

/*静态全员变量位置*/
static int mysock = INVALID_SOCKET; //用来标记sock错误的。

/*这里是用来定义一些功能的与MQTTSN_magTypes对应，*/
static char* packet_names[] =
{
		"ADVERTISE", "SEARCHGW", "GWINFO", "RESERVED", "CONNECT", "CONNACK",
		"WILLTOPICREQ", "WILLTOPIC", "WILLMSGREQ", "WILLMSG", "REGISTER", "REGACK",
		"PUBLISH", "PUBACK", "PUBCOMP", "PUBREC", "PUBREL", "RESERVED",
		"SUBSCRIBE", "SUBACK", "UNSUBSCRIBE", "UNSUBACK", "PINGREQ", "PINGRESP",
		"DISCONNECT", "RESERVED", "WILLTOPICUPD", "WILLTOPICRESP", "WILLMSGUPD",
		"WILLMSGRESP"
};

/*这里定义一些需要映射的信息，TopicId所对应的消息头，写死还是写成动态的？*/
/*先选择写死了*/
static char * topID[20];



/*数据类型定义*/
typedef struct
{
	MQTTSN_topicTypes type;
	union
	{
		unsigned short id;
		char short_name[2];
		struct
		{
			char* name;
			int len;
		} long_;
	} data;
} MQTTSN_topicid;

typedef struct
{
	int len;
	char* data;
} MQTTSNLenString;

typedef struct
{
	char* cstring;
	MQTTSNLenString lenstring;
} MQTTSNString;

typedef struct
{
	
	char struct_id[4];  /** 这里是MQSC就行了，固定 */
	int struct_version; 	/** 结构的的版本号，必须是0，要求是官方的初始版本，目前网关用的就是初始版本。*/
	MQTTSNString clientID;  /*客户端的ID，模拟的c+的string*/
	unsigned short duration; /*心跳时间的值暂时还不清楚怎么弄这个心跳*/
	unsigned char cleansession; /*与MQTT的含义相同，但对Will主题和Will消息进行了扩展*/
	unsigned char willFlag;     /*如果设置，表明客户正在请求获得Will主题和Will消息提示。will消息暂时不清楚，不使用。*/
} MQTTSNPacket_connectData;

typedef union
{
    /*这个函数作用主要是为了给MQTTSNPacket_connectData，但是这个内连体的all具体干嘛不知道，消息包好像写了，又好像没写，日*/
	unsigned char all;
	struct
	{
		unsigned int topicIdType : 2;
		unsigned int cleanSession : 1;
		unsigned int will : 1;
		unsigned int retain : 1;
		unsigned int QoS : 2;
		int dup: 1;
	} bits;
} MQTTSNFlags;

/*****************************************************
 *       dyl 
 * 
 *       函数定义位置
 * 
 ******************************************************/

int MQTTSNSerialize_connect(unsigned char* buf, int buflen, MQTTSNPacket_connectData* options);
int MQTTSNPacket_len(int length);
int MQTTSNSerialize_connectLength(MQTTSNPacket_connectData* options);
int MQTTSNstrlen(MQTTSNString MQTTSNString);
void writeChar(unsigned char** pptr, char c);
void writeInt(unsigned char** pptr, int anInt);
void writeMQTTSNString(unsigned char** pptr, MQTTSNString MQTTSNString);
void writeCString(unsigned char** pptr, char* string);
int MQTTSNPacket_encode(unsigned char* buf, int length);
int MQTTSNPacket_read(unsigned char* buf, int buflen, int (*getfn)(unsigned char*, int));
int MQTTSNPacket_decode(unsigned char* buf, int buflen, int* value);
int readInt(unsigned char** pptr);
int transport_getdata(unsigned char* buf, int count);
int transport_sendPacketBuffer(char* host, int port, unsigned char* buf, int buflen);
int Socket_error(char* aString, int sock);
int transport_open();
int transport_close();
/*到此为止，以上函数都是按照连接的时候该使用那个，就掉那个*/
int MQTTSNSerialize_register(unsigned char* buf, int buflen, unsigned short topicid, unsigned short packetid,MQTTSNString* topicname);
int MQTTSNSerialize_registerLength(int topicnamelen);
int MQTTSNDeserialize_regack(unsigned short* topicid, unsigned short* packetid, unsigned char* return_code,unsigned char* buf, int buflen);
char readChar(unsigned char** pptr);
/*********************pub***********************************/
int MQTTSNSerialize_publish(unsigned char* buf, int buflen, unsigned char dup, int qos, unsigned char retained, unsigned short packetid,MQTTSN_topicid topic, unsigned char* payload, int payloadlen);
int MQTTSNSerialize_publishLength(int payloadlen, MQTTSN_topicid topic, int qos);
/*********************sub***********************************/
int MQTTSNSerialize_subscribe(unsigned char* buf, int buflen, unsigned char dup, int qos, unsigned short packetid,MQTTSN_topicid* topicFilter);
int MQTTSNDeserialize_suback(int* qos, unsigned short* topicid, unsigned short* packetid,unsigned char* returncode, unsigned char* buf, int buflen);
int MQTTSNSerialize_subscribeLength(MQTTSN_topicid* topicFilter);
/*********************recive*******************************/
/****************************************************
 *            dyl
 * 
 *                      主函数
 * 			connect，pub，sub。
 * 
 ****************************************************/

int main()
{
    MQTTSN_topicid topic;      //主要用的
    MQTTSNString topicstr;     //cstr 一个定义的可变长字符串
    unsigned char buf[512];	   //  缓冲区。
    int buflen = sizeof(buf);
    int len = 0;			  
    int retained = 0;         //暂时用不到。
    char *topicname = "MyTopicName";
    char *ClientID  = "dyl";				//id具有唯一性
    char *host      = "192.168.2.163";      //服务器地址没试过域名登录。
    int port = 1884;						//端口号 mqttsn的为1884
    unsigned char message[512] = "emmp dyl 测试 ";	//消息，push之后用。
    /*初始化*/
    MQTTSNPacket_connectData options = MQTTSNPacket_connectData_initializer;
    /*发送消息或者订阅的ID，需要映射*/
    unsigned short TopicID = 1;
    printf("--------------------------\n");
    /* ************************************************/
    /*用来添加一个socket的标识符，mysock是一个静态的全局变量*/
    mysock = transport_open(); 
    /*   ****************************************** */
    options.clientID.cstring = ClientID;
	/***********************分割线***********************************/
	/*连接*/
    /*这里面主要是用来封装buff的连接信息*/
    len = MQTTSNSerialize_connect(buf,buflen,&options);
    printf("%d\n",len);    //失败返回

    // for(int i=0;i<len;i++) printf("%d ",buf[i]);
    // printf("\n------------------------------------\n");
    int sn = transport_sendPacketBuffer(host,port,buf,len);
    // printf("sn1 == %d\n",sn);
    // sn = MQTTSNPacket_read((unsigned char *)buf,buflen,transport_getdata);
    // printf("sn2 == %d\n",sn);
    // if(sn==MQTTSN_CONNACK)
    // {
    //     printf("进入connack\n");
    // }
    if(MQTTSNPacket_read(buf,buflen,transport_getdata)==MQTTSN_CONNACK)
    {
        for(int i=0;i<buflen;i++) printf("%d ",buf[i]);
		char *buff = "\nconnack\n";
		int  datalen = 0;
		printf("%s",buff);
    }
	/*************************分割线*****************************************/
	/*这个步骤是一个日本网友写的，也是订阅，但是貌似不成功，而且这个看着sub合理点*/
	/**
	 * @brief 2.0
	 * bug1，已解决，需要从MQTTSN1.2文档里面看，具体这个没改。
	 *      1，遗嘱消息目前不了解。
	 * 		2，sub确实是订阅待，解决怎么弄。
	 */
	int packid = 1;
	topicstr.cstring = topicname;
	topicstr.lenstring.len = strlen(topic.data.short_name);
	len = MQTTSNSerialize_register(buf,buflen,1,packid,&topicstr);
	int rc = transport_sendPacketBuffer(host,port,buf,len);
	printf("rc topstr %d\n",rc);
	if(MQTTSNPacket_read(buf,buflen,transport_getdata) == MQTTSN_REGACK)
    {
        unsigned short submsgid;
        unsigned char returuncode;
        rc = MQTTSNDeserialize_regack(&TopicID,&submsgid,&returuncode,buf,buflen);
        if(returuncode != 0)
        {
            printf("return code %d\n",returuncode);
        }
        else
        {
            printf("regack topic id %d\n",TopicID);
        }
    }

    /***************************分割线**************************************************/
	/*发送*/ /*这个是qos0真一点问题也没*/
	    			printf("Publishing\n");
    topic.type = MQTTSN_TOPIC_TYPE_NORMAL;
    topic.data.id = TopicID;

    int messagelen = strlen((const char *)message);
    len = MQTTSNSerialize_publish(buf,buflen,0,0,retained,0,topic,message,messagelen);
    rc = transport_sendPacketBuffer(host,port,buf,len);
    printf("rc %d from send packet for publish length %d\n", rc, len);
	/*****************************分割线*************************************************/
	/*订阅 存在bug尚未解决*/
	/**
	 * @brief 1.0
	 * bug1   概率段错误，没找到原因。
	 * bug2	  订阅的主题不对，胡乱订阅的。
	 * 
	 * 俩问题，解决问题。
	 */
// subscr:	
					printf("SUB\n");
	unsigned short pid = 1;
	topic.data.long_.name = "MYTOPIC";
	topic.data.long_.len = sizeof("MYTOPIC");
	// buflen = strlen(buf);
	len = MQTTSNSerialize_subscribe(buf,buflen,0,0,packid,&topic);	
	// rc = MQTTSNDeserialize_suback(0,&TopicID,&pid,message,buf,buflen);
	printf("SUB rc = %d\n",len);
	// if(len < 0)
	// {
	// 	goto subscr;
	// }
	rc = transport_sendPacketBuffer(host,port,buf,len);
	if(rc >= 0)
	{
		printf("------sunccess------\nSUB rc1 = %d\n",rc);
	}
	else
	{
		printf("------error------\nSUB rc1 = %d\n",rc);
	}
	unsigned short submsgid = 1;
    unsigned char returuncode;
	if(MQTTSNPacket_read(buf,buflen,transport_getdata) == MQTTSN_SUBACK)
	{
		rc = MQTTSNDeserialize_regack(&TopicID,&submsgid,&returuncode,buf,buflen);
		printf("SUBMSGID =  %d rc = %d\n",submsgid,rc);
		printf("topicid = %d\n",TopicID);
        if(returuncode != 0)
        {
            printf("SUB return code %d\n",returuncode);
        }
        else
        {
            printf("SUB regack topic id %d\n",TopicID);
        }
	}

	/*******************************分割线***********************************/
	/*接收*/
	/**
	 * 问题，没有现成的接受
	 * 
	 */
	*/

	
}


/**************************************************************************
*
*	  dyl
*     函数实现位置
*
***************************************************************************/

/**
 * 返回MQTTSNString-C字符串的长度，如果有的话，否则就是长度限定的字符串。
 * @param MQTTSNString 要返回长度的字符串
 * @return the length of the string
 */
int MQTTSNstrlen(MQTTSNString MQTTSNString)
{
	int rc = 0;

	if (MQTTSNString.cstring)
		rc = strlen(MQTTSNString.cstring);
	else
		rc = MQTTSNString.lenstring.len;
	return rc;
}

/**
  * 确定MQTT连接数据包的长度，该数据包将使用所提供的连接选项产生。
  * @param options 用来建立连接数据包的选项。
  * @return 包含数据包的序列化版本所需的缓冲区的长度
  */
int MQTTSNSerialize_connectLength(MQTTSNPacket_connectData* options)
{
	int len = 0;
	len = 5 + MQTTSNstrlen(options->clientID);
	return len;
}


/**
  *将连接选项序列化到缓冲区。
  * @param buf 数据包将被序列化的缓冲区。
  * @param len 所提供的缓冲区的长度（字节）。
  * @param options 用来建立连接数据包的选项
  * @返回序列化的长度，如果为0，则为错误。
  */
int MQTTSNSerialize_connect(unsigned char* buf, int buflen, MQTTSNPacket_connectData* options)
{
	unsigned char *ptr = buf;
	MQTTSNFlags flags;
	int len = 0;
	int rc = -1;

	if ((len = MQTTSNPacket_len(MQTTSNSerialize_connectLength(options))) > buflen)
	{
		rc = MQTTSNPACKET_BUFFER_TOO_SHORT;
		goto exit;
	}
	ptr += MQTTSNPacket_encode(ptr, len); /* write length */
	writeChar(&ptr, MQTTSN_CONNECT);      /* write message type */

	flags.all = 0;
	flags.bits.cleanSession = options->cleansession;
	flags.bits.will = options->willFlag;
	writeChar(&ptr, flags.all);
	writeChar(&ptr, 0x01);                 /* protocol ID */
	writeInt(&ptr, options->duration);
	writeMQTTSNString(&ptr, options->clientID);
	rc = ptr - buf;
exit:
    printf("rc connect1 %d\n",rc);
	return rc;
}


void writeMQTTSNString(unsigned char** pptr, MQTTSNString MQTTSNString)
{
	if (MQTTSNString.lenstring.len > 0)
	{
		memcpy(*pptr, (const unsigned char*)MQTTSNString.lenstring.data, MQTTSNString.lenstring.len);
		*pptr += MQTTSNString.lenstring.len;
	}
	else if (MQTTSNString.cstring)
		writeCString(pptr, MQTTSNString.cstring);
}


/**
 *将一个整数作为2个字节写入输出缓冲区。
 * @param pptr 指向输出缓冲区的指针 - 以使用的字节数递增并返回。
 * @param anInt 要写入的整数。0到65535
 */
void writeInt(unsigned char** pptr, int anInt)
{
	**pptr = (unsigned char)(anInt / 256);
	(*pptr)++;
	**pptr = (unsigned char)(anInt % 256);
	(*pptr)++;
}

/**
 * 写一个字符到一个输出缓冲区。
 * @param pptr 指向输出缓冲区的指针 - 以使用的字节数递增并返回。
 * @param c 要写的字符
 */
void writeChar(unsigned char** pptr, char c)
{
	**pptr = (unsigned char)c;
	(*pptr)++;
}

/**
 * 写一个 "UTF "字符串到输出缓冲区。 将C语言字符串转换为以长度为界限的字符串。
 * @param pptr 指向输出缓冲区的指针 - 以使用和返回的字节数递增。
 * @param string 要写入的C语言字符串
 */
void writeCString(unsigned char** pptr, char* string)
{
	int len = strlen(string);
	memcpy(*pptr, string, len);
	*pptr += len;
}

/**
 *对MQTT-SN消息的长度进行编码
 * @param buf 缓冲区，编码后的数据被写入其中
 * @param length 是要编码的长度
 * @return 写入缓冲区的字节数
 */
int MQTTSNPacket_encode(unsigned char* buf, int length)
{
	int rc = 0;

	if (length >= 255)
	{
		writeChar(&buf, 0x01);
		writeInt(&buf, length);
		rc += 3;
	}
	else
		buf[rc++] = length;

	return rc;
}

/**
 * 计算包括长度字段在内的全部数据包长度
 *@param length 不含长度字段的MQTT-SN数据包的长度
 *@return 。MQTT-SN数据包的总长度，包括长度字段。
 */
int MQTTSNPacket_len(int length)
{
	return (length >= 255) ? length + 3 : length + 1;
}


/**
 * 将数据包从某个源头读入一个缓冲区的辅助函数
 * @param buf 缓冲区，数据包将被序列化到其中。
 * @param buflen 所提供的缓冲区的长度（字节）。
 * @param getfn 指向一个函数的指针，该函数将从需要的来源读取任何数量的字节。
 * @return integer MQTT packet type, or MQTTSNPACKET_READ_ERROR on error
 */
int MQTTSNPacket_read(unsigned char* buf, int buflen, int (*getfn)(unsigned char*, int))
{
	int rc = MQTTSNPACKET_READ_ERROR;
	const int MQTTSN_MIN_PACKET_LENGTH = 2;
	int len = 0;  /* the length of the whole packet including length field */
	int lenlen = 0;
	int datalen = 0;

	/* 1. read a packet - UDP style */
	if ((len = (*getfn)(buf, buflen)) < MQTTSN_MIN_PACKET_LENGTH)
        // printf("len1 %d\n",len);
		goto exit;
    // printf("rc1 %d\n",rc);
	/* 2. read the length.  This is variable in itself */
	lenlen = MQTTSNPacket_decode(buf, len, &datalen);
	if (datalen != len)
		goto exit; /* there was an error */
    // printf("rc2 %d\n",rc);
	rc = buf[lenlen]; /* 用来返回交互的类型 */
exit:
    printf("rc3 %d lenlen = %d\n",rc,lenlen);
	return rc;
}


/**
 *从收到的数据中获取MQTT-SN数据包的长度
 * @param buf是包含MQTT-SN数据包的缓冲区。
 * @param buflen 所提供的缓冲区的长度（字节）。
 * @param value 返回的解码长度
 * @return 从套接字中读取的字节数
 */
int MQTTSNPacket_decode(unsigned char* buf, int buflen, int* value)
{
	int len = MQTTSNPACKET_READ_ERROR;
#define MAX_NO_OF_LENGTH_BYTES 3

	if (buflen <= 0)
		goto exit;

	if (buf[0] == 1)
	{
		unsigned char* bufptr = &buf[1];
		if (buflen < MAX_NO_OF_LENGTH_BYTES)
			goto exit;
		*value = readInt(&bufptr);
		len = 3;
	}
	else
	{
		*value = buf[0];
		len = 1;
	}
exit:
	return len;
}


/**
 *从输入缓冲区读出的两个字节中计算出一个整数
 * @param pptr 指向输入缓冲区的指针 - 以使用和返回的字节数递增
 * @return 计算出的整数值
 */
int readInt(unsigned char** pptr)
{
	unsigned char* ptr = *pptr;
	int len = 256*((unsigned char)(*ptr)) + (unsigned char)(*(ptr+1));
	*pptr += 2;
	return len;
}

int transport_getdata(unsigned char* buf, int count)
{

	int rc = recvfrom(mysock, (char *)buf, count, 0, NULL, NULL);
	printf("received %d bytes count %d\n", rc, (int)count);
	return rc;
}

int transport_sendPacketBuffer(char* host, int port, unsigned char* buf, int buflen)
{
	struct sockaddr_in cliaddr;
	int rc = 0;

	memset(&cliaddr, 0, sizeof(cliaddr));
	cliaddr.sin_family = AF_INET;
	cliaddr.sin_addr.s_addr = inet_addr(host);
	cliaddr.sin_port = htons(port);

	if ((rc = sendto(mysock, (char *)buf, buflen, 0, (const struct sockaddr*)&cliaddr, sizeof(cliaddr))) == SOCKET_ERROR)
		Socket_error("sendto", mysock);
	else
		rc = 0;
	return rc;
}


int Socket_error(char* aString, int sock)
{
	if (errno != EINTR && errno != EAGAIN && errno != EINPROGRESS && errno != EWOULDBLOCK)
	{
		if (strcmp(aString, "shutdown") != 0 || (errno != ENOTCONN && errno != ECONNRESET))
		{
			int orig_errno = errno;
			char* errmsg = strerror(errno);

			printf("Socket error %d (%s) in %s for socket %d\n", orig_errno, errmsg, aString, sock);
		}
	}
	return errno;
}

int transport_open()
{
	mysock = socket(AF_INET, SOCK_DGRAM, 0);
	if (mysock == INVALID_SOCKET)
		return Socket_error("socket", mysock);

	return mysock;
}

int transport_close()
{
	int rc;
	rc = shutdown(mysock, SHUT_WR);
	rc = close(mysock);

	return rc;
}

/**
  *将提供的寄存器数据序列化到提供的缓冲区中，准备发送。
  * @param buf 缓冲区，数据包将被序列化到其中。
  * @param buflen 所提供的缓冲区的长度（字节）。
  * @param topicid 如果由网关发送，则包含话题名称的ID，否则为0
  * @param packetid 整数 - MQTT数据包的标识符
  * @param topicname为空尾的主题名称字符串
  * @return the length of the serialized data.  <= 0表示错误
  */
int MQTTSNSerialize_register(unsigned char* buf, int buflen, unsigned short topicid, unsigned short packetid,MQTTSNString* topicname)
{
	unsigned char *ptr = buf;
	int len = 0;
	int rc = 0;
	int topicnamelen = 0;

	topicnamelen = (topicname->cstring) ? (int)strlen(topicname->cstring) : topicname->lenstring.len;
	if ((len = MQTTSNPacket_len(MQTTSNSerialize_registerLength(topicnamelen))) > buflen)
	{
		rc = MQTTSNPACKET_BUFFER_TOO_SHORT;
		goto exit;
	}
	ptr += MQTTSNPacket_encode(ptr, len);  /* write length */
	writeChar(&ptr, MQTTSN_REGISTER);      /* write message type */

	writeInt(&ptr, topicid);
	writeInt(&ptr, packetid);

	memcpy(ptr, (topicname->cstring) ? topicname->cstring : topicname->lenstring.data, topicnamelen);
	ptr += topicnamelen;

	rc = ptr - buf;
exit:
	return rc;
}


/**
  * 确定MQTT寄存器数据包的长度，该数据包将使用所提供的参数产生。
  * @param topicnamelen 寄存器中使用的主题名称的长度
  *@return 包含数据包的序列化版本所需的缓冲区的长度
  */
int MQTTSNSerialize_registerLength(int topicnamelen)
{
	return topicnamelen + 5;
}


/**
  *将提供的（线）缓冲区反序列化为寄存器数据
  * @param topicid 返回主题ID
  * @param packetid 返回整数 - MQTT数据包的标识符
  * @param return_code 返回整数的返回代码
  * @param buf 原始缓冲区数据，正确的长度由剩余长度字段决定。
  * @param buflen 所提供的缓冲区中的数据长度（字节）。
  *@return 错误代码。 1为成功
  */
int MQTTSNDeserialize_regack(unsigned short* topicid, unsigned short* packetid, unsigned char* return_code,unsigned char* buf, int buflen)
{
	unsigned char* curdata = buf;
	unsigned char* enddata = NULL;
	int rc = 0;
	int mylen = 0;

	curdata += (rc = MQTTSNPacket_decode(curdata, buflen, &mylen)); /* read length */
	enddata = buf + mylen;
	if (enddata - curdata > buflen)
		goto exit;

	if (readChar(&curdata) != MQTTSN_REGACK)
		goto exit;

	*topicid = readInt(&curdata);
	*packetid = readInt(&curdata);
	*return_code = readChar(&curdata);

	rc = 1;
exit:
	return rc;
}

/**
 *从输入缓冲区读取一个字符。
 * @param pptr 指向输入缓冲区的指针 - 以使用的字节数递增并返回
 * @return 读取的字符
 */
char readChar(unsigned char** pptr)
{
	char c = **pptr;
	(*pptr)++;
	return c;
}

/**
  *将提供的发布数据序列化到提供的缓冲区中，准备发送。
  * @param buf 缓冲区，数据包将被序列化到其中。
  * @param buflen 所提供的缓冲区的长度（字节）。
  * @param dup integer - MQTT的dup标志
  * @param qos integer - MQTT的QoS值
  * @param retained integer - MQTT保留标志
  * @param packetid 整数 - MQTT数据包标识符
  * @param topic MQTTSN_topicid - 发布中的MQTT主题
  * @param payload byte buffer - MQTT发布的有效载荷
  * @param payloadlen integer - MQTT有效载荷的长度
  * @return 串行化数据的长度。 <= 0 表示错误
  */
int MQTTSNSerialize_publish(unsigned char* buf, int buflen, unsigned char dup, int qos, unsigned char retained, unsigned short packetid,MQTTSN_topicid topic, unsigned char* payload, int payloadlen)
{
	unsigned char *ptr = buf;
	MQTTSNFlags flags;
	int len = 0;
	int rc = 0;

	if ((len = MQTTSNPacket_len(MQTTSNSerialize_publishLength(payloadlen, topic, qos))) > buflen)
	{
		rc = MQTTSNPACKET_BUFFER_TOO_SHORT;
		goto exit;
	}
	ptr += MQTTSNPacket_encode(ptr, len); /* write length */
	writeChar(&ptr, MQTTSN_PUBLISH);      /* write message type */

	flags.all = 0;
	flags.bits.dup = dup;
	flags.bits.QoS = qos;
	flags.bits.retain = retained;
	flags.bits.topicIdType = topic.type;
	writeChar(&ptr, flags.all);

	if (topic.type == MQTTSN_TOPIC_TYPE_NORMAL && qos == 3)
	{
		/* 在QoS-1的发布中，对长的主题名称进行特殊安排。 主题的长度在topicid字段中。 */
		writeInt(&ptr, topic.data.long_.len); /* topic length */
	}
	else if (topic.type == MQTTSN_TOPIC_TYPE_NORMAL || topic.type == MQTTSN_TOPIC_TYPE_PREDEFINED)
		writeInt(&ptr, topic.data.id);
	else
	{
		writeChar(&ptr, topic.data.short_name[0]);
		writeChar(&ptr, topic.data.short_name[1]);
	}
	writeInt(&ptr, packetid);
	if (topic.type == MQTTSN_TOPIC_TYPE_NORMAL && qos == 3)
	{
		memcpy(ptr, topic.data.long_.name, topic.data.long_.len);
		ptr += topic.data.long_.len;
	}
	memcpy(ptr, payload, payloadlen);
	ptr += payloadlen;

	rc = ptr - buf;
exit:
	return rc;
}


/**
  * 确定使用所提供的参数产生的MQTT发布数据包的长度。
  * @param qos 发布的MQTT QoS (packetid对于QoS 0来说是省略的)
  * @param topicName 将在发布中使用的主题名称。 
  * @param payloadlen 要发送的有效载荷的长度
  * @返回包含数据包的序列化版本所需的缓冲区的长度
  */
int MQTTSNSerialize_publishLength(int payloadlen, MQTTSN_topicid topic, int qos)
{
	int len = 6;

	if (topic.type == MQTTSN_TOPIC_TYPE_NORMAL && qos == 3)
		len += topic.data.long_.len;

	return payloadlen + len;
}


/**
  *将提供的订阅数据序列化到提供的缓冲区中，准备发送。
  * @param buf 缓冲区，数据包将被序列化到其中。
  * @param buflen 所提供的缓冲区的长度（字节）。
  * @param dup integer - MQTT-SN的dup标志
  * @param qos integer - MQTT-SN的QoS值
  * @param packetid 整数 - MQTT-SN的数据包标识符
  * @param topic MQTTSN_topicid - 订阅中的MQTT-SN主题
  * @return 串行化数据的长度。 <= 0 表示错误
  */
int MQTTSNSerialize_subscribe(unsigned char* buf, int buflen, unsigned char dup, int qos, unsigned short packetid,MQTTSN_topicid* topicFilter)
{
	unsigned char *ptr = buf;
	MQTTSNFlags flags;
	int len = 0;
	int rc = 0;
	if ((len = MQTTSNPacket_len(MQTTSNSerialize_subscribeLength(topicFilter))) > buflen)
	{
		rc = MQTTSNPACKET_BUFFER_TOO_SHORT;
		goto exit;
	}
	ptr += MQTTSNPacket_encode(ptr, len);   /* write length */
	writeChar(&ptr, MQTTSN_SUBSCRIBE);      /* write message type */

	flags.all = 0;
	flags.bits.dup = dup;
	flags.bits.QoS = qos;
	flags.bits.topicIdType = topicFilter->type;
	writeChar(&ptr, flags.all);

	writeInt(&ptr, packetid);

	/* now the topic id or name */
	if (topicFilter->type == MQTTSN_TOPIC_TYPE_NORMAL) /* means long topic name */
	{
		memcpy(ptr, topicFilter->data.long_.name, topicFilter->data.long_.len);
		ptr += topicFilter->data.long_.len;
	}
	else if (topicFilter->type == MQTTSN_TOPIC_TYPE_PREDEFINED)
		writeInt(&ptr, topicFilter->data.id);
	else if (topicFilter->type == MQTTSN_TOPIC_TYPE_SHORT)
	{
		writeChar(&ptr, topicFilter->data.short_name[0]);
		writeChar(&ptr, topicFilter->data.short_name[1]);
	}
	rc = ptr - buf;
exit:
	return rc;
}



/**
  * 将提供的（线）缓冲区反序列化为suback数据。
  * @param qos 返回的qos
  * @param topicid返回 - 如果 "接受"，该值将被网关用于随后的PUBLISH包中。
  * @param packetid 返回 - 与相应的SUBSCRIBE中的值相同。
  * @param returncode 返回 - "接受 "或拒绝的原因
  * @param buf 原始缓冲区数据，正确的长度由剩余长度字段决定。
  * @param buflen 所提供的缓冲区中数据的长度（字节）。
  *@return 错误代码。 1为成功
  */
int MQTTSNDeserialize_suback(int* qos, unsigned short* topicid, unsigned short* packetid,unsigned char* returncode, unsigned char* buf, int buflen)
{
	MQTTSNFlags flags;
	unsigned char* curdata = buf;
	unsigned char* enddata = NULL;
	int rc = 0;
	int mylen = 0;

	curdata += (rc = MQTTSNPacket_decode(curdata, buflen, &mylen)); /* read length */
	enddata = buf + mylen;
	if (enddata - curdata > buflen)
		goto exit;

	if (readChar(&curdata) != MQTTSN_SUBACK)
		goto exit;

	flags.all = readChar(&curdata);
	*qos = flags.bits.QoS;

	*topicid = readInt(&curdata);
	*packetid = readInt(&curdata);
	*returncode = readChar(&curdata);

	rc = 1;
exit:
	return rc;
}


/**
  * 确定MQTTSN订阅数据包的长度，该数据包将使用所提供的参数产生。
  * 不包括长度
  * param topicName 将在发布中使用的主题名称。 
  * @return 包含数据包的序列化版本所需的缓冲区的长度
  */
int MQTTSNSerialize_subscribeLength(MQTTSN_topicid* topicFilter)
{
	int len = 4;

	if (topicFilter->type == MQTTSN_TOPIC_TYPE_NORMAL)
		len += topicFilter->data.long_.len;
	else if (topicFilter->type == MQTTSN_TOPIC_TYPE_SHORT || topicFilter->type == MQTTSN_TOPIC_TYPE_PREDEFINED)
		len += 2;
		
	return len;
}
