# 一，MQTT-SN简介

1，MQTT-SN（Sensor Networks）是MQTT协议的传感器版本，最早使用在zigBee无线网络中，主要面对电池供电有限的处理器能力和存储能力的设备。基于TCP协议的MQTT对有些传感器来说还是负载太重了，这些传感器可能只有几十个字节的内存，无法运行TCP协议。

2，总之，针对低功耗、电池驱动、处理存储受限的设备、不支持TCP/IP协议栈网络的电子器件而定制，对所依赖的底层传输网络不可知，但只要网络支持双向数据传输和网关，都是可以支持较为上层的MQTT-SN协议传输。

# 二，MQTT-SN   vs  MQTT

1， CONNECT消息被拆分成三个消息（CONNECT，WILLTIPIC，WILLMSG），后两者用于客户端传递遗嘱主题和遗嘱消息等
2， 在PUBLISH消息中主题（topic name）被替换成两个字节长度自然数（topic id），这个需要客户端通过注册流程进行获取对应的topic id
3， 预定义（提前定义）topic id和topic name，省去中间注册流程，客户端和网关要求提前在其固件中指定
4， 协议引入的自动发现机制可帮助客户端发现潜在的网关。若存在多个网关，彼此可协调是为主从互备或者负载均衡
5， "clean session"即可作用于订阅持久化，也被扩展作用于遗嘱特性（遗嘱主题和遗嘱消息）
6， 针对休眠设备增加离线保活机制支持，当有消息时代理需要缓存，客户端被唤醒时再发送

# 三，Qos机制

| QoS Level | 消息传输次数 | 传输语义 | 传输保证                                              |
| --------- | ------------ | -------- | ----------------------------------------------------- |
| -1        | ≤ 1          | 至多一次 | 无连接，只用于传输，尽力而为，无保证；只有MQTT-SN支持 |
| 0         | ≤ 1          | 至多一次 | 尽力而为，无保证                                      |
| 1         | ≥ 1          | 至少一次 | 保证送达，可能存在重复                                |
| -1        | ≡ 1          | 只有一次 | 保证送达，并且不存在重复                              |

# 四，网关的查询和发现

1. MQTT-SN客户端无须提前知道网关地址，接收网关的周期性广播好了，或直接广播一条网关查询；接收广播+主动查询，就够了
2. MQTT-SN对客户端/网关的乱入和丢失，处理的很好，备用网关在主网关挂掉之后即可转换为主网关，针对客户端而言，直接更新一个新的网关地址就可以
3. 存在的若干网关可以互相协调彼此之间角色，互为主备stand-by，出现问题，主机下线维护，从机升级主机

# 五，清理会话

和MQTT 3.1协议类似，在上一次的客户端成功连接时在CONNECT中设置了清理会话标志clean session为false，遗嘱特性Will也为true，再次连接时，那么服务器为其缓存订阅数据和遗嘱数据是否已经被删除，对客户端不透明，因为就算是服务器因内存压力等清理了缓存，但没有通知到客户端，会造成订阅、遗嘱的误解。

还好，MQTT-SN协议中，网关在清理掉遗嘱数据后，可以咨询客户端，或主动通知客户端断开，重新建立会话流程。

在MQTT 3.1.1协议中，服务器会在CONNACK中标记会话是否已经被持久的标记。

# 六，主题标识符

确切来说，MQTT-SN协议存在三种格式主题名称（topic name），可由消息标志位包含TopicIdType属性决定：

- 0b01：预分配的主题标识符（topic id），16位自然数，0-65535范围
- 0b10：预分配的短主题名称（short topic name），只有两个字符表示
- 0b00：正常主题名称（topic name），可直接附加在REGISTER/SUBSCRIBE/WILLTOPICUPD消息对应字段中

所有主题被替换成标识符，在发布PUBLISH消息时，直接使用被指定的主题标识符topic id、short topic name即可。

# 七，MQTT-SN流程梳理

```
              Client              Gateway            Server/Broker
                |                   |                    |
Generic Process | --- SERCHGW ----> |                    |
                | <-- GWINFO  ----- |                    |
                | --- CONNECT ----> |                    |
                | <--WILLTOPICREQ-- |                    |
                | --- WILLTOPIC --> |                    |
                | <-- WILLMSGREQ -- |                    |
                | --- WILLMSG ----> | ---- CONNECT ----> |(accepted)
                | <-- CONNACK ----- | <--- CONNACK ----- |
                | --- PUBLISH ----> |                    |
                | <-- PUBACK  ----- | (invalid TopicId)  |
                | --- REGISTER ---> |                    |
                | <-- REGACK  ----- |                    |
                | --- PUBLISH ----> | ---- PUBLISH ----> |(accepted)
                | <-- PUBACK  ----- | <---- PUBACK ----- |
                |                   |                    |
                //                  //                   //
                |                   |                    |
 SUBSCRIBE   -->| --- SUBSCRIBE --> | ---- SUBSCRIBE --> |
 [var Callback] | <-- SUBACK ------ | <--- SUBACK ------ |
                |                   |                    |
                //                  //                   //
                |                   |                    |
                | <-- REGISTER ---- | <--- PUBLISH ----- |<-- PUBLISH
                | --- REGACK  ----> |                    |
[exec Callback] | <-- PUBLISH  ---- |                    |
                | --- PUBACK   ---> | ---- PUBACK  ----> |--> PUBACK
                |                   |                    |
                //                  //                   //
                |                   |                    |
active -> asleep| --- DISCONNECT -> | (with duration)    |
                | <-- DISCONNECT -- | (without duration) |
                |                   |                    |
                //                  //                   //
                |                   |                    |
                |                   | <--- PUBLISH ----- |<-- PUBLISH
                |                   | ----- PUBACK ----> |
                |                   | <--- PUBLISH ----- |<-- PUBLISH
                |                   | ----- PUBACK ----> |
                |                   | (buffered messages)|
asleep -> awake |                   |                    |
                | --- PINGREQ ----> |                    |
awake state     | <-- PUBLISH  ---- |                    |
                | <-- PUBLISH  ---- |                    |
                | <-- PINGRESP ---- |                    |
asleep <-awake  |                   |                    |
```

