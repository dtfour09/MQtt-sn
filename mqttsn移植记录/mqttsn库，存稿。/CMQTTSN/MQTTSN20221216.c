/***********************************************************************************************
 * @file MQTTSN.c
 * @author dyl
 * @brief 
 * @version 0.1
 * @date 2022-12-04
 * 
 * @copyright Copyright (c) 2022
 * 
 * MQTTSN�ͻ��ˣ�md����һ���ļ��õ�vcԶ�����ӵ�Linux�����������ֲ��Ƕ��ʽ�����С�
 * �󲿷ִ��뻹�� �ٷ���packet����ֻ�������˲ü������ڹٷ����ĵ����ǲ�̫��⡣
 * �㹻���Ļ�����ʵ�ʵ�ȥ�޸Ĳ�ͬ��packet����
 * ��ʵûɶ�޸ĵ���Ҫ��pub�������߹����ֽڵĴ���
 **********************************************************************************************/
/*ͷ�ļ�λ��*/
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
/*�궨��λ��*/
#define SOCKET_ERROR -1                          //�׽��ֱ���
#define INVALID_SOCKET SOCKET_ERROR
#define MQTTSNPacket_connectData_initializer { {'M', 'Q', 'S', 'C'}, 0, {NULL, {0, NULL}}, 10, 1, 0 } //����ʱ��ĳ�ʼ��

/*ȫ�ֱ���λ��*/
typedef enum
{
	MQTTSN_TOPIC_TYPE_NORMAL, /* topic id in publish, topic name in subscribe */
	MQTTSN_TOPIC_TYPE_PREDEFINED,
	MQTTSN_TOPIC_TYPE_SHORT
}MQTTSN_topicTypes;


enum MQTTSN_msgTypes
{
    /*MQTTSN��Ϣ������*/
	MQTTSN_ADVERTISE, MQTTSN_SEARCHGW, MQTTSN_GWINFO, MQTTSN_RESERVED1, //���أ��ͻ��˲���
	MQTTSN_CONNECT, MQTTSN_CONNACK,										//���ӣ��ͻ��˵ġ�
	MQTTSN_WILLTOPICREQ, MQTTSN_WILLTOPIC, MQTTSN_WILLMSGREQ, MQTTSN_WILLMSG, //������ʱ���˽⡣
	MQTTSN_REGISTER, MQTTSN_REGACK,									//Ӧ��Ҳ����������̫�˽⣬����ʲ��ǡ�
	MQTTSN_PUBLISH, MQTTSN_PUBACK, MQTTSN_PUBCOMP, MQTTSN_PUBREC, MQTTSN_PUBREL, MQTTSN_RESERVED2,//pub
	MQTTSN_SUBSCRIBE, MQTTSN_SUBACK, MQTTSN_UNSUBSCRIBE, MQTTSN_UNSUBACK, //sub
	MQTTSN_PINGREQ, MQTTSN_PINGRESP,			//���Ӧ��������
	MQTTSN_DISCONNECT, MQTTSN_RESERVED3, 		//�Ͽ�����
	MQTTSN_WILLTOPICUPD, MQTTSN_WILLTOPICRESP, MQTTSN_WILLMSGUPD, MQTTSN_WILLMSGRESP, //�����������
	MQTTSN_ENCAPSULATED = 0xfe					//����λ��
};

enum errors
{
    /*����λ�ã���ʱ�������ɡ�*/
	MQTTSNPACKET_BUFFER_TOO_SHORT = -2,
	MQTTSNPACKET_READ_ERROR = -1,
	MQTTSNPACKET_READ_COMPLETE
};

/*��̬ȫԱ����λ��*/
static int mysock = INVALID_SOCKET; //�������sock����ġ�

/*��������������һЩ���ܵ���MQTTSN_magTypes��Ӧ��*/
static char* packet_names[] =
{
		"ADVERTISE", "SEARCHGW", "GWINFO", "RESERVED", "CONNECT", "CONNACK",
		"WILLTOPICREQ", "WILLTOPIC", "WILLMSGREQ", "WILLMSG", "REGISTER", "REGACK",
		"PUBLISH", "PUBACK", "PUBCOMP", "PUBREC", "PUBREL", "RESERVED",
		"SUBSCRIBE", "SUBACK", "UNSUBSCRIBE", "UNSUBACK", "PINGREQ", "PINGRESP",
		"DISCONNECT", "RESERVED", "WILLTOPICUPD", "WILLTOPICRESP", "WILLMSGUPD",
		"WILLMSGRESP"
};

/*���ﶨ��һЩ��Ҫӳ�����Ϣ��TopicId����Ӧ����Ϣͷ��д������д�ɶ�̬�ģ�*/
/*��ѡ��д����*/
static char * topID[20];



/*�������Ͷ���*/
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
	
	char struct_id[4];  /** ������MQSC�����ˣ��̶� */
	int struct_version; 	/** �ṹ�ĵİ汾�ţ�������0��Ҫ���ǹٷ��ĳ�ʼ�汾��Ŀǰ�����õľ��ǳ�ʼ�汾��*/
	MQTTSNString clientID;  /*�ͻ��˵�ID��ģ���c+��string*/
	unsigned short duration; /*����ʱ���ֵ��ʱ���������ôŪ�������*/
	unsigned char cleansession; /*��MQTT�ĺ�����ͬ������Will�����Will��Ϣ��������չ*/
	unsigned char willFlag;     /*������ã������ͻ�����������Will�����Will��Ϣ��ʾ��will��Ϣ��ʱ���������ʹ�á�*/
} MQTTSNPacket_connectData;

typedef union
{
    /*�������������Ҫ��Ϊ�˸�MQTTSNPacket_connectData����������������all������ﲻ֪������Ϣ������д�ˣ��ֺ���ûд����*/
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
 *       ��������λ��
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
/*����Ϊֹ�����Ϻ������ǰ������ӵ�ʱ���ʹ���Ǹ����͵��Ǹ�*/
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
 *                      ������
 * 			connect��pub��sub��
 * 
 ****************************************************/

int main()
{
    MQTTSN_topicid topic;      //��Ҫ�õ�
    MQTTSNString topicstr;     //cstr һ������Ŀɱ䳤�ַ���
    unsigned char buf[512];	   //  ��������
    int buflen = sizeof(buf);
    int len = 0;			  
    int retained = 0;         //��ʱ�ò�����
    char *topicname = "MyTopicName";
    char *ClientID  = "dyl";				//id����Ψһ��
    char *host      = "192.168.2.163";      //��������ַû�Թ�������¼��
    int port = 1884;						//�˿ں� mqttsn��Ϊ1884
    unsigned char message[512] = "emmp dyl ���� ";	//��Ϣ��push֮���á�
    /*��ʼ��*/
    MQTTSNPacket_connectData options = MQTTSNPacket_connectData_initializer;
    /*������Ϣ���߶��ĵ�ID����Ҫӳ��*/
    unsigned short TopicID = 1;
    printf("--------------------------\n");
    /* ************************************************/
    /*�������һ��socket�ı�ʶ����mysock��һ����̬��ȫ�ֱ���*/
    mysock = transport_open(); 
    /*   ****************************************** */
    options.clientID.cstring = ClientID;
	/***********************�ָ���***********************************/
	/*����*/
    /*��������Ҫ��������װbuff��������Ϣ*/
    len = MQTTSNSerialize_connect(buf,buflen,&options);
    printf("%d\n",len);    //ʧ�ܷ���

    // for(int i=0;i<len;i++) printf("%d ",buf[i]);
    // printf("\n------------------------------------\n");
    int sn = transport_sendPacketBuffer(host,port,buf,len);
    // printf("sn1 == %d\n",sn);
    // sn = MQTTSNPacket_read((unsigned char *)buf,buflen,transport_getdata);
    // printf("sn2 == %d\n",sn);
    // if(sn==MQTTSN_CONNACK)
    // {
    //     printf("����connack\n");
    // }
    if(MQTTSNPacket_read(buf,buflen,transport_getdata)==MQTTSN_CONNACK)
    {
        for(int i=0;i<buflen;i++) printf("%d ",buf[i]);
		char *buff = "\nconnack\n";
		int  datalen = 0;
		printf("%s",buff);
    }
	/*************************�ָ���*****************************************/
	/*���������һ���ձ�����д�ģ�Ҳ�Ƕ��ģ�����ò�Ʋ��ɹ��������������sub�����*/
	/**
	 * @brief 2.0
	 * bug1���ѽ������Ҫ��MQTTSN1.2�ĵ����濴���������û�ġ�
	 *      1��������ϢĿǰ���˽⡣
	 * 		2��subȷʵ�Ƕ��Ĵ��������ôŪ��
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

    /***************************�ָ���**************************************************/
	/*����*/ /*�����qos0��һ������Ҳû*/
	    			printf("Publishing\n");
    topic.type = MQTTSN_TOPIC_TYPE_NORMAL;
    topic.data.id = TopicID;

    int messagelen = strlen((const char *)message);
    len = MQTTSNSerialize_publish(buf,buflen,0,0,retained,0,topic,message,messagelen);
    rc = transport_sendPacketBuffer(host,port,buf,len);
    printf("rc %d from send packet for publish length %d\n", rc, len);
	/*****************************�ָ���*************************************************/
	/*���� ����bug��δ���*/
	/**
	 * @brief 1.0
	 * bug1   ���ʶδ���û�ҵ�ԭ��
	 * bug2	  ���ĵ����ⲻ�ԣ����Ҷ��ĵġ�
	 * 
	 * �����⣬������⡣
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

	/*******************************�ָ���***********************************/
	/*����*/
	/**
	 * ���⣬û���ֳɵĽ���
	 * 
	 */
	*/

	
}


/**************************************************************************
*
*	  dyl
*     ����ʵ��λ��
*
***************************************************************************/

/**
 * ����MQTTSNString-C�ַ����ĳ��ȣ�����еĻ���������ǳ����޶����ַ�����
 * @param MQTTSNString Ҫ���س��ȵ��ַ���
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
  * ȷ��MQTT�������ݰ��ĳ��ȣ������ݰ���ʹ�����ṩ������ѡ�������
  * @param options ���������������ݰ���ѡ�
  * @return �������ݰ������л��汾����Ļ������ĳ���
  */
int MQTTSNSerialize_connectLength(MQTTSNPacket_connectData* options)
{
	int len = 0;
	len = 5 + MQTTSNstrlen(options->clientID);
	return len;
}


/**
  *������ѡ�����л�����������
  * @param buf ���ݰ��������л��Ļ�������
  * @param len ���ṩ�Ļ������ĳ��ȣ��ֽڣ���
  * @param options ���������������ݰ���ѡ��
  * @�������л��ĳ��ȣ����Ϊ0����Ϊ����
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
 *��һ��������Ϊ2���ֽ�д�������������
 * @param pptr ָ�������������ָ�� - ��ʹ�õ��ֽ������������ء�
 * @param anInt Ҫд���������0��65535
 */
void writeInt(unsigned char** pptr, int anInt)
{
	**pptr = (unsigned char)(anInt / 256);
	(*pptr)++;
	**pptr = (unsigned char)(anInt % 256);
	(*pptr)++;
}

/**
 * дһ���ַ���һ�������������
 * @param pptr ָ�������������ָ�� - ��ʹ�õ��ֽ������������ء�
 * @param c Ҫд���ַ�
 */
void writeChar(unsigned char** pptr, char c)
{
	**pptr = (unsigned char)c;
	(*pptr)++;
}

/**
 * дһ�� "UTF "�ַ���������������� ��C�����ַ���ת��Ϊ�Գ���Ϊ���޵��ַ�����
 * @param pptr ָ�������������ָ�� - ��ʹ�úͷ��ص��ֽ���������
 * @param string Ҫд���C�����ַ���
 */
void writeCString(unsigned char** pptr, char* string)
{
	int len = strlen(string);
	memcpy(*pptr, string, len);
	*pptr += len;
}

/**
 *��MQTT-SN��Ϣ�ĳ��Ƚ��б���
 * @param buf �����������������ݱ�д������
 * @param length ��Ҫ����ĳ���
 * @return д�뻺�������ֽ���
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
 * ������������ֶ����ڵ�ȫ�����ݰ�����
 *@param length ���������ֶε�MQTT-SN���ݰ��ĳ���
 *@return ��MQTT-SN���ݰ����ܳ��ȣ����������ֶΡ�
 */
int MQTTSNPacket_len(int length)
{
	return (length >= 255) ? length + 3 : length + 1;
}


/**
 * �����ݰ���ĳ��Դͷ����һ���������ĸ�������
 * @param buf �����������ݰ��������л������С�
 * @param buflen ���ṩ�Ļ������ĳ��ȣ��ֽڣ���
 * @param getfn ָ��һ��������ָ�룬�ú���������Ҫ����Դ��ȡ�κ��������ֽڡ�
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
	rc = buf[lenlen]; /* �������ؽ��������� */
exit:
    printf("rc3 %d lenlen = %d\n",rc,lenlen);
	return rc;
}


/**
 *���յ��������л�ȡMQTT-SN���ݰ��ĳ���
 * @param buf�ǰ���MQTT-SN���ݰ��Ļ�������
 * @param buflen ���ṩ�Ļ������ĳ��ȣ��ֽڣ���
 * @param value ���صĽ��볤��
 * @return ���׽����ж�ȡ���ֽ���
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
 *�����뻺���������������ֽ��м����һ������
 * @param pptr ָ�����뻺������ָ�� - ��ʹ�úͷ��ص��ֽ�������
 * @return �����������ֵ
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
  *���ṩ�ļĴ����������л����ṩ�Ļ������У�׼�����͡�
  * @param buf �����������ݰ��������л������С�
  * @param buflen ���ṩ�Ļ������ĳ��ȣ��ֽڣ���
  * @param topicid ��������ط��ͣ�������������Ƶ�ID������Ϊ0
  * @param packetid ���� - MQTT���ݰ��ı�ʶ��
  * @param topicnameΪ��β�����������ַ���
  * @return the length of the serialized data.  <= 0��ʾ����
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
  * ȷ��MQTT�Ĵ������ݰ��ĳ��ȣ������ݰ���ʹ�����ṩ�Ĳ���������
  * @param topicnamelen �Ĵ�����ʹ�õ��������Ƶĳ���
  *@return �������ݰ������л��汾����Ļ������ĳ���
  */
int MQTTSNSerialize_registerLength(int topicnamelen)
{
	return topicnamelen + 5;
}


/**
  *���ṩ�ģ��ߣ������������л�Ϊ�Ĵ�������
  * @param topicid ��������ID
  * @param packetid �������� - MQTT���ݰ��ı�ʶ��
  * @param return_code ���������ķ��ش���
  * @param buf ԭʼ���������ݣ���ȷ�ĳ�����ʣ�೤���ֶξ�����
  * @param buflen ���ṩ�Ļ������е����ݳ��ȣ��ֽڣ���
  *@return ������롣 1Ϊ�ɹ�
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
 *�����뻺������ȡһ���ַ���
 * @param pptr ָ�����뻺������ָ�� - ��ʹ�õ��ֽ�������������
 * @return ��ȡ���ַ�
 */
char readChar(unsigned char** pptr)
{
	char c = **pptr;
	(*pptr)++;
	return c;
}

/**
  *���ṩ�ķ����������л����ṩ�Ļ������У�׼�����͡�
  * @param buf �����������ݰ��������л������С�
  * @param buflen ���ṩ�Ļ������ĳ��ȣ��ֽڣ���
  * @param dup integer - MQTT��dup��־
  * @param qos integer - MQTT��QoSֵ
  * @param retained integer - MQTT������־
  * @param packetid ���� - MQTT���ݰ���ʶ��
  * @param topic MQTTSN_topicid - �����е�MQTT����
  * @param payload byte buffer - MQTT��������Ч�غ�
  * @param payloadlen integer - MQTT��Ч�غɵĳ���
  * @return ���л����ݵĳ��ȡ� <= 0 ��ʾ����
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
		/* ��QoS-1�ķ����У��Գ����������ƽ������ⰲ�š� ����ĳ�����topicid�ֶ��С� */
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
  * ȷ��ʹ�����ṩ�Ĳ���������MQTT�������ݰ��ĳ��ȡ�
  * @param qos ������MQTT QoS (packetid����QoS 0��˵��ʡ�Ե�)
  * @param topicName ���ڷ�����ʹ�õ��������ơ� 
  * @param payloadlen Ҫ���͵���Ч�غɵĳ���
  * @���ذ������ݰ������л��汾����Ļ������ĳ���
  */
int MQTTSNSerialize_publishLength(int payloadlen, MQTTSN_topicid topic, int qos)
{
	int len = 6;

	if (topic.type == MQTTSN_TOPIC_TYPE_NORMAL && qos == 3)
		len += topic.data.long_.len;

	return payloadlen + len;
}


/**
  *���ṩ�Ķ����������л����ṩ�Ļ������У�׼�����͡�
  * @param buf �����������ݰ��������л������С�
  * @param buflen ���ṩ�Ļ������ĳ��ȣ��ֽڣ���
  * @param dup integer - MQTT-SN��dup��־
  * @param qos integer - MQTT-SN��QoSֵ
  * @param packetid ���� - MQTT-SN�����ݰ���ʶ��
  * @param topic MQTTSN_topicid - �����е�MQTT-SN����
  * @return ���л����ݵĳ��ȡ� <= 0 ��ʾ����
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
  * ���ṩ�ģ��ߣ������������л�Ϊsuback���ݡ�
  * @param qos ���ص�qos
  * @param topicid���� - ��� "����"����ֵ����������������PUBLISH���С�
  * @param packetid ���� - ����Ӧ��SUBSCRIBE�е�ֵ��ͬ��
  * @param returncode ���� - "���� "��ܾ���ԭ��
  * @param buf ԭʼ���������ݣ���ȷ�ĳ�����ʣ�೤���ֶξ�����
  * @param buflen ���ṩ�Ļ����������ݵĳ��ȣ��ֽڣ���
  *@return ������롣 1Ϊ�ɹ�
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
  * ȷ��MQTTSN�������ݰ��ĳ��ȣ������ݰ���ʹ�����ṩ�Ĳ���������
  * ����������
  * param topicName ���ڷ�����ʹ�õ��������ơ� 
  * @return �������ݰ������л��汾����Ļ������ĳ���
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
