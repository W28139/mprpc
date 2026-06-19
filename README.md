# mprpc - 基于 C++ 的高性能分布式 RPC 框架

`mprpc` 是一个基于 **muduo**、**Protobuf** 和 **Zookeeper** 实现的高性能 C++ 分布式 RPC 框架。它通过解耦业务逻辑与网络通信，实现了服务方动态注册、消费方自动发现以及跨节点的远程方法调用。

## 项目核心特性

*   **高性能网络引擎**：基于 `muduo` 网络库实现 Reactor 模型，利用非阻塞 I/O 和多线程事件循环处理高并发连接。
*   **透明化 RPC 调用**：通过继承 `google::protobuf::RpcChannel` 并封装 `CallMethod`，实现像调用本地方法一样发起远程调用。
*   **动态服务发现**：集成 `Zookeeper` 充当注册中心，支持 Provider 节点自动上线/下线感知，解决了分布式环境下寻址困难的问题。
*   **反射机制集成**：深度利用 Protobuf 的反射 API，在服务端实现动态服务映射与方法调度，无需为每个接口硬编码处理逻辑。
*   **自定义通信协议**：设计 `[header_size] + [RpcHeader] + [args]` 协议报文，通过固定长度头部解析，彻底解决 TCP 粘包/半包问题。
*   **异步日志系统**：基于生产者-消费者模型实现的异步日志，利用 `LockQueue` 缓冲区确保高频 RPC 调用下日志 I/O 不阻塞业务线程。

## 模块架构

项目由以下六大模块组成：

1.  **基础环境与配置模块 (Application/Config)**：加载并维护 `rpcserver` 和 `zookeeper` 的 IP 与端口信息。
2.  **Protobuf 协议与序列化模块**：定义 RPC 请求/响应格式，负责数据的二进制序列化与反序列化。
3.  **muduo 网络通信模块 (RpcProvider)**：封装底层 TCP 监听与收发逻辑，实现请求的解包、路由与分发。
4.  **mprpc 核心代理与状态控制模块 (Channel/Controller)**：客户端 Stub 代理实现，以及 RPC 过程中的错误追踪与状态管理。
5.  **Zookeeper 分布式协调模块 (ZkClient)**：提供服务注册、动态发现及心跳维护功能。
6.  **异步日志记录模块 (Logger)**：支持 `INFO` 和 `ERROR` 级别的异步落盘，保障框架运行的可观测性。

## 环境依赖

*   **操作系统**：Linux (Ubuntu/CentOS)
*   **编译器**：g++ (支持 C++11)
*   **构建工具**：CMake
*   **依赖库**：
    *   `Protobuf` (v3.x 或以上)
    *   `muduo` (高性能网络库)
    *   `Zookeeper C Client` (mt 版本)

## 快速开始

### 1. 编译安装
```bash
git clone https://W28139/github.com/mprpc.git
cd mprpc
mkdir build
cd build
cmake ..
make
```

### 2. 配置文件 (`test.conf`)
```ini
# RPC 服务器地址
rpcserverip=127.0.0.1
rpcserverport=8000
# Zookeeper 地址
zookeeperip=127.0.0.1
zookeeperport=2181
```

### 3. 运行服务
```bash
# 启动 Zookeeper 服务
zkServer.sh start

# 启动 Provider (服务端)
./provider -i test.conf

# 启动 Consumer (消费端)
./consumer -i test.conf
```

## 📝 业务应用示例 (example)

在 `example` 目录下提供了完整的应用案例：
1.  **定义接口**：在 `.proto` 文件中声明 `Service` 与 `Method`。
2.  **实现服务端**：继承并重写本地业务函数，通过 `NotifyService` 发布到框架。
3.  **实现客户端**：通过 `stub` 发起远程调用，并利用 `Controller` 获取框架层面的错误状态。

---

### 💡 技术亮点：异步日志的设计与重构
在开发过程中，为了解决异步环境下多线程竞态导致日志级别覆盖的 Bug，本项目将 `LogLevel` 与 `Message` 封装为 `LogMsg` 结构体，作为对象在 `LockQueue` 中流转，确保了在高并发请求下日志记录的准确性与线程安全性。

---


# 一、mprpc 框架使用全梳理

使用 mprpc 开发分布式服务的流程可以分为 **定义合同、实现服务端、实现客户端** 三个核心阶段。

## 第一阶段：定义合同 (Protocol Buffers)

一切分布式调用的基础是 `.proto` 文件。

1.  **定义消息体**：确定请求（Request）和响应（Response）的数据格式。
2.  **定义服务 (Service)**：在 `.proto` 中声明远程方法名。
3.  **生成代码**：使用 `protoc` 编译生成 `.pb.h` 和 `.pb.cc`。
    *   **作用**：保证服务端和客户端拥有一致的序列化/反序列化标准和方法存根（Stub）。

## 第二阶段：服务端开发 (RpcProvider)

服务端的主要任务是“发布服务”并“处理请求”。

1.  **业务类继承与重写**：
    *   继承由 `.proto` 生成的 `fixbug::FriendServiceRpc`。
    *   **本地业务代码**：写具体的逻辑（如 `GetFriendsList` 真正查询数据库的操作）。
    *   **RPC 包装接口**：重写框架调用的虚函数。
        *   *细节*：从 `Request` 拿参数，调用本地逻辑，将结果填充进 `Response`，最后调用 `done->Run()` 让框架执行后续的序列化和网络发送。

2.  **框架初始化 (`MprpcApplication::Init`)**：
    *   **作用**：读取配置文件（如 `test.conf`）。
    *   **IP/Port 配置流向**：
        *   读取 `rpcserverip` 和 `rpcserverport`：用于配置本地 `muduo` 网络库的监听地址。
        *   读取 `zookeeperip` 和 `zookeeperport`：用于后续将服务注册到 Zookeeper 中心。

3.  **服务注册 (`NotifyService`)**：
    *   **作用**：将 `FriendService` 对象注册到 `RpcProvider` 的一个内部 `Map` 中。
    *   **细节**：框架会利用 Protobuf 的反射机制，自动提取出服务名和方法名，建立“服务名->方法名->方法描述”的映射表。

4.  **启动服务 (`provider.Run`)**：
    *   **开启网络监听**：启动 `muduo` 的 `TcpServer`。
    *   **ZK 注册**：连接 Zookeeper，将当前服务器发布的每一个服务名和方法名作为路径（如 `/FriendServiceRpc/GetFriendsList`），并将自己的 IP:Port 写入该节点。

## 第三阶段：客户端开发 (MprpcChannel)

客户端的主要任务是“发现服务”并“发起调用”。

1.  **框架初始化 (`MprpcApplication::Init`)**：
    *   **作用**：同样是读取配置文件。
    *   **IP/Port 配置流向**：客户端主要读取 `zookeeperip` 和 `zookeeperport`。它不需要知道服务端的固定 IP，因为它会去 ZK 查。

2.  **创建通道与存根 (`Stub`)**：
    *   `fixbug::FriendServiceRpc_Stub stub(new MprpcChannel());`
    *   **核心逻辑**：所有的远程调用最终都会进入 `MprpcChannel::CallMethod`。这是 RPC 的灵魂，它负责：
        *   去 Zookeeper 查找目标方法所在的 IP 和端口。
        *   序列化请求参数。
        *   通过 TCP 发送数据并同步等待返回。

3.  **发起调用**：
    *   填充 `request`，创建 `response` 和 `MprpcController`。
    *   `stub.GetFriendsList(...)`。
    *   **结果处理**：通过 `controller.Failed()` 判断网络是否有错，通过 `response.result().errcode()` 判断业务是否有错。

---

### IP 和 Port 到底配置到了哪里？

这是面试中常问的细节，你可以这样回答：

1.  **在服务端 (`provider.cc`)**：
    *   配置文件的 IP/Port 被传入了 `muduo` 网络库的 `TcpServer` 中，用于 `bind` 和 `listen`。
    *   同时，这一组 IP/Port 被发送到了 Zookeeper，存放在以方法名为路径的节点数据中。

2.  **在客户端 (`mprpcchannel.cc`)**：
    *   客户端**不配置**服务端的 IP/Port。
    *   它从配置文件读取 **Zookeeper 的地址**。
    *   在发起调用的瞬间，它根据方法名去 Zookeeper 动态查询，实时拿到服务端的 IP/Port，然后执行 `connect`。



# 二、解析项目框架

## 第一部分：MprpcApplication与MprpcConfig类

这两个类服务器与客户端都会创建，他俩配合，一个调用一个读取取配置信息，作为单例，为后面代码提供IP和port信息，仅此而已，没有其他作用

## 第二部分：RpcProvider类（由各个服务器调用）

它是最核心的类，其中有以下几个成员函数：

* **NotifyService**：把业务对象转换为框架可识别的映射表。
* **Run**：启动网络监听 + 注册到 Zookeeper。
* **OnMessage / OnConnection**：协议解析 $\rightarrow$ 反射查找 $\rightarrow$ 动态构造 Request $\rightarrow$ 调用业务接口。
* **SendRpcResponse**：业务执行完毕后的收尾，负责把结果序列化并推回网络。

其中，NotifyService和Run是直接写给服务器主程序调用

### 1. 服务发布期：NotifyService

主程序调用 `NotifyService` 时，框架并不是简单地存下对象，而是通过 **Protobuf 的反射机制**，对该服务进行“全身扫描”：

*   **获取元数据**：利用 `ServiceDescriptor` 拿到服务名（如 `FriendServiceRpc`）和它包含的所有方法名（如 `GetFriendsList`）。
*   **构建映射表**：将服务对象和它所有的方法描述符封装进一个 `ServiceInfo` 结构体，存入 `m_serviceMap`。
*   **意义**：这步操作就像是在后台建了一个“路由表”。当请求来时，框架能根据字符串瞬间定位到该执行哪个对象的哪个方法。

1.  **`service`**：这是 `new` 出来的业务对象（动态运行时的**实体**）。
2.  **`ServiceDescriptor`**：这是服务的**元数据**（静态的结构信息）。它告诉你这个 Service 有哪些 Method。
3.  **`MethodDescriptor`**：这是方法的**元数据**。它告诉你这个 Method 需要哪种 Message。
4.  **`Message` (Request/Response)**：这是数据的**载体**。

| Protobuf 类名                   | 作用                                                         |
| :------------------------------ | :----------------------------------------------------------- |
| **`google::protobuf::Service`** | 它是你定义的 `FriendService` 的父类。它有一个 `CallMethod` 函数，负责执行真正的业务逻辑。 |
| **`GetDescriptor()`**           | 通过这个函数，可以拿到这个“管理员”到底能干哪些活的说明书。   |
| **`ServiceDescriptor`**         | 记录了这个服务叫什么名字（`FriendServiceRpc`），里面一共有多少个方法。 |
| **`MethodDescriptor`**          | 记录了某个具体方法的名字（`GetFriendsList`），以及它的参数需要什么类型的 Request，返回什么类型的 Response。 |

### 2. 网络监听期：Run

调用 `Run` 是让服务正式上线的指令：

*   **启动网络引擎**：根据 `MprpcApplication` 提供的配置，初始化 `muduo::net::TcpServer`，绑定 `OnConnection` 和 `OnMessage` 回调。
*   **同步注册中心**：连接 Zookeeper，将 `m_serviceMap` 中记录的所有服务名和方法名作为路径，将当前服务器的 `IP:Port` 写入 ZK 临时节点。
*   **进入循环**：调用 `server.start()` 和 `pool.loop()`，开始阻塞式监听网络事件。

### 3. 请求调度期：OnMessage (核心转换)

这是最复杂的阶段，体现了 RPC 协议的拆解过程。当收到数据包时，框架按以下步骤操作：

#### 第一步：解包（数据拆解）

TCP 是流式传输，会存在粘包问题。框架按照自定义协议解析（在rpcheader.proto里定义）：

1.  读取前 **4 个字节**：得知 `header_size`（头部长度）。
2.  根据 `header_size` 读取内容：反序列化得到 `RpcHeader`，从中提取 `service_name`、`method_name` 和 `args_size`。
3.  读取接下来的 `args_size` 个字节：拿到真正的方法请求参数。

#### 第二步：反射查找（逻辑定位）

1.  根据 `service_name` 从 `m_serviceMap` 中找到对应的 `Service` 对象。
2.  根据 `method_name` 找到对应的 `MethodDescriptor`（方法描述符）。

#### 第三步：构造参数（动态创建）

这步利用了 Protobuf 的魔法：

1.  **Request 构造**：通过 `service->GetRequestPrototype(method).New()` 动态创建一个该方法专属的请求对象。
2.  **反序列化**：将之前提取的 `args` 填入这个 Request。
3.  **Response 构造**：同理，创建一个空的响应对象。

#### 第四步：执行回调（真正调用）

1.  **绑定 Closure（闭包）**：这是最巧妙的设计。给 `service->CallMethod` 传入一个回调函数 `SendRpcResponse`。
    *   *逻辑*：当业务逻辑执行完并调用 `done->Run()` 时，程序会自动跳转到 `SendRpcResponse`，将处理完的 Response 序列化并通过 `muduo` 发回给客户端。
2.  **派发请求**：调用 `service->CallMethod(...)`。此时，程序正式跳出框架层，进入你在 `example` 里写的业务逻辑代码。

---

### 4. 辅助函数：SendRpcResponse

它是闭包的回调实现。它的职责非常单一：

*   将业务层填好的 `response` 序列化为字节流。
*   通过 `muduo` 的 `TcpConnection::send` 发送出去。
*   **善后工作**：主动断开连接（短连接模式），或者保持连接（长连接模式），由配置决定。

### 5.  其中涉及的Protobuf 的反射机制

#### Protobuf 的反射机制的必要性

在`mprpc` 框架中，如果没有反射，每增加一个服务，都要手动写一堆 `if-else` 或 `switch` 逻辑来判断到底调用哪个函数。有了反射，框架就能像“自动导航”一样，根据字符串名字自动找到对应的类和函数。

#### 反射机制在在代码逻辑中的体现

反射主要体现在两个阶段：**NotifyService（注册期）** 和 **OnMessage（调用期）**。

##### A. NotifyService：反射用于“扫描”

当把 `FriendService` 传给框架时，框架并不认识这个类，它会通过反射“摸骨”：

1. **获取描述**：`const google::protobuf::ServiceDescriptor* pserviceDesc = service->GetDescriptor();`

   *   *翻译*：框架问：“你这个服务长啥样啊？” 拿到一份关于该服务的详细说明书。

2. **获取名字**：`std::string service_name = pserviceDesc->name();`

   *   *翻译*：从说明书里看到服务名字叫 `FriendServiceRpc`。

3. **遍历方法**：

   ```cpp
   int methodCnt = pserviceDesc->method_count(); // 查查说明书里有几个方法
   for (int i=0; i<methodCnt; ++i) 
   {
       const google::protobuf::MethodDescriptor* pmethodDesc = pserviceDesc->method(i);
       std::string method_name = pmethodDesc->name(); // 拿到具体方法名
   }
   ```

   **反射的作用**：框架不需要提前知道 `FriendService`，它在程序运行时动态“读”出了这个类里的所有信息。

---

##### B. OnMessage：反射用于“动态构造”

这是反射最神奇的地方。当收到 `GetFriendsList` 的请求时，框架需要创建一个 `GetFriendsListRequest` 对象，但框架的代码里并没有写 `new GetFriendsListRequest`。

框架是怎么做的呢？

1. **找到原型**：

   ```cpp
   // 根据 method 描述符，找到这个方法需要的请求对象的“模板”
   const google::protobuf::Message* prototype = service->GetRequestPrototype(pmethodDesc);
   ```

2. **克隆新对象**：

   ```cpp
   // 根据模板，“克隆”出一个全新的、干净的请求对象实例
   google::protobuf::Message* request = prototype->New();
   ```

3. **反序列化**：
   `request->ParseFromString(args_str);` // 把网络传来的字节流填进这个新对象。

**反射的作用**：框架实现了**通用性**。它不需要为 `FriendService` 写一套代码，为 `UserService` 写一套代码。它通过 `MethodDescriptor` 就像拿到了模具，能根据不同的方法现场生产出不同的 Request 对象。

#### 为什么项目中要这么写？

**为了实现解耦。**

如果没有这些 `Descriptor`：

*   当你的 RPC 服务器收到数据包时，你必须写：
    `if (name == "GetFriendsList") { GetFriendsListRequest req; ... }`
*   这意味着每加一个方法，你都要改框架代码。

**有了这些反射名词：**

*   框架变成了：`Message* req = service->GetRequestPrototype(method)->New();`
*   无论你以后加 100 个还是 1000 个方法，**框架代码一行都不用改**。它会根据 Zookeeper 传来的字符串，自动去映射表里找到对应的描述符，动态创建对象并执行。

**这就是反射的威力：它让框架具备了处理“未来还未编写的类”的能力。**





## 第三部分：MprpcChannel 类（由客户端调用）

`MprpcChannel` 的核心职责是：**将本地的函数调用转化为网络数据包，并实现服务发现。** 它让客户端开发者感觉像在调用本地函数一样简单，而所有的复杂逻辑都隐藏在框架中。

### 1. 业务逻辑向框架的转换（透明代理机制）

这是整个 RPC 流程中最精妙的设计。转换过程分为以下三个层级：

1.  **业务层调用**：客户端代码调用 `stub.GetFriendsList(&controller,&request,&response,nullptr)`。对业务开发人员来说，这就是个普通的 C++ 成员函数。
2.  **存根中介（Stub）**：`Stub` 类是 Protobuf 自动生成的。它的内部并不包含业务逻辑，而是包含一行核心代码：`channel_->CallMethod(...)`。
3.  **进入框架（MprpcChannel）**：
    *   `MprpcChannel` 继承自 `google::protobuf::RpcChannel` 并重写了纯虚函数 `CallMethod`。
    *   **通用性体现**：无论你调用的是 `Login`、`Register` 还是 `GetFriendsList`，它们最终都会汇聚到这同一个 `CallMethod` 函数中。
    *   **元数据传递**：框架通过 `MethodDescriptor` 这一参数，动态地知道了本次调用所属的 **服务名** 和 **方法名**。

---

重写的很关键，客户端构造stub时，CallMethod里面什么也没有，客户端下面调用的时候就无法调用，如果框架对其进行重写，那就有了对应逻辑传递的内容，客户端就可以调用了

### 2. CallMethod 内部的一系列核心操作

当业务请求进入 `CallMethod` 后，框架会按部就班地执行以下“五部曲”：

#### 第一步：参数序列化

*   **动作**：将用户传入的 `request` 对象序列化为二进制字符串 `args_str`。
*   **目的**：对象无法直接在网络上传输，必须转为字节流。

#### 第二步：协议封装（组织报文）

*   **动作**：构造并序列化 `RpcHeader`（包含服务名、方法名、参数长度），并计算其长度 `header_size`。
*   **打包格式**：`[4字节 header_size] + [RpcHeader序列化串] + [args参数序列化串]`。
*   **目的**：解决 **粘包问题**。前 4 个字节告诉服务端：接下来该读多少字节的头部信息，从而准确解析出业务参数。

#### 第三步：服务发现（动态找人）—— 2.0 核心改进（1.0是写死，没有zk)

*   **动作**：
    1.  实例化 `ZkClient` 并启动。
    2.  根据 `/ServiceName/MethodName` 组织路径，去 Zookeeper 查找。
    3.  从 Zookeeper 节点中获取存储的 `ip:port` 字符串。
*   **目的**：摆脱静态配置文件，实现真正的分布式动态调用。如果服务器地址变了，客户端能通过 ZK 实时感知。

#### 第四步：网络通信（同步阻塞调用）

*   **动作**：
    1.  创建客户端 Socket。
    2.  解析 ZK 返回的 IP 和端口，执行 `connect` 连接 RPC 服务端。
    3.  `send` 发送打包好的数据包。
    4.  `recv` 阻塞等待服务端的响应数据。
*   **细节**：这里使用的是同步阻塞模式，即请求发出后，当前线程会挂起，直到收到回包。
*   这里直接socket，因为客户端不会有高并发，完全用不到muduo

#### 第五步：反序列化（结果回归）

*   **动作**：将接收到的二进制回包解析回 `response` 对象。
*   **目的**：将结果交还给业务层，让调用方能直接读取到 `response.result()` 等信息。

---

### 3. 框架的通用性是如何实现的？

“保证通用性”关键点在于 `CallMethod` 的参数设计：

*   **`Message* request`** 和 **`Message* response`**：使用了 Protobuf 的基类指针。
*   这使得框架不需要关心你具体传的是 `LoginRequest` 还是 `GetFriendsRequest`。
*   **逻辑闭环**：框架只负责搬运字节流、查 Zookeeper 和拆解协议。至于字节流里代表的是什么业务，由服务端对应的 `OnMessage` 里的反射机制去解析。



## 第四部分：mprpccontroller

### 1. 核心作用：区分“业务错误”与“框架错误”

在普通的 C++ 函数调用中，通过返回值就能判断对错。但在 RPC 这种分布式场景下，情况要复杂得多：

*   **业务逻辑错误**（路通了，事没办成）：比如登录时“密码错误”。这通常写在 `.proto` 定义的 `Response` 消息体里。
*   **RPC 框架/网络错误**（路断了，没见到人）：比如“Zookeeper 连不上”、“目标服务器宕机”、“网络包序列化失败”。

**`MprpcController` 的存在，就是专门用来记录和传递这些“框架/网络层”的错误信息的。** 它是客户端了解 RPC 调用是否“平安到达”的唯一途径。

---

### 2. 它的调用轨迹：谁在用它？

它在代码中的生命周期非常清晰：

#### A. 客户端主程序：发起者与查看者

在 `main.cpp` 中，创建它并把它传给 `stub`。

```cpp
MprpcController controller; // 1. 创建监控仪（默认状态：OK）

// 2. 把它作为参数传进去。注意：CallMethod 内部会修改这个对象的状态
stub.GetFriendsList(&controller, &request, &response, nullptr);

// 3. 调用结束后，查看监控仪记录的结果
if (controller.Failed()) {
    std::cout << controller.ErrorText() << std::endl; // 打印具体的框架错误原因
} else {
    // 只有框架没报错，去读 response 里的业务数据才有意义
}
```

#### B. MprpcChannel::CallMethod：执行者与记录者

这是 `controller` 真正干活的地方。在 `mprpcchannel.cc` 源码里，会大量对它的调用：

*   **序列化失败时**：
    `controller->SetFailed("serialize request error!"); return;`
*   **Zookeeper 查不到地址时**：
    `controller->SetFailed(method_path + " is not exist!"); return;`
*   **网络连接（connect）失败时**：
    `controller->SetFailed("connect error!"); return;`
*   **发送/接收数据失败时**：
    `controller->SetFailed("recv error!"); return;`

---

### 3. 核心成员函数解析

源码中，重点关注这三个函数：

1.  **`SetFailed(const std::string& reason)`**：
    *   **谁调？** 框架内部（`MprpcChannel`）。
    *   **干啥？** 把内部标志位 `m_failed` 设为 `true`，并把具体的错误话术（如 "errno:111"）存进 `m_errText`。
2.  **`Failed()`**：
    *   **谁调？** 客户端业务层（`main` 函数）。
    *   **干啥？** 询问：“刚才那次远程调用，框架层面出问题了吗？”
3.  **`ErrorText()`**：
    *   **谁调？** 客户端业务层。
    *   **干啥？** “如果出问题了，告诉我具体原因是什么。”

---

### 4. 为什么要通过参数传递？（设计模式）

你可能会问：为什么不让 `CallMethod` 直接返回一个错误码，而非要传个对象？

*   **符合 Protobuf 标准接口**：`google::protobuf::RpcChannel::CallMethod` 的原生接口定义就是这样。遵循标准可以保证你的框架能无缝对接任何基于 Protobuf 的系统。
*   **解耦与扩展**：`Controller` 对象可以承载比简单错误码更丰富的信息（比如错误文本、是否超时、是否取消调用等）。虽然你目前只实现了错误记录，但它预留了将来实现“调用超时控制”或“主动取消请求”的能力。



**MprpcController 的一句话总结：**
它是 RPC 调用过程中的**状态追踪器**。

*   **在客户端 `main` 里**：它是**查询器**，用来判断远程调用是否成功到达。
*   **在框架 `CallMethod` 里**：它是**记录器**，一旦遇到网络或协议错误，就立即调用 `SetFailed` 并中断流程。



## 第五部分：ZooKeeperUtil 类（分布式协调与服务发现）

### 1. 失去它的痛点：

如果没有 Zookeeper 这样的协调中心，分布式系统会面临以下崩溃场景：

*   **配置地狱（瞎）**：客户端（Consumer）必须在配置文件里写死每一个服务端的 IP。一旦服务端迁移、重启导致 IP 变动，所有客户端都必须跟着改配置并重启。
*   **状态感知缺失（聋）**：服务端挂了，客户端根本不知道，请求依然会源源不断地发往“死机”，导致大量请求超时报错。
*   **扩容瓶颈**：想新加一台服务器来分担压力？你得去通知所有的客户端：我这里多了一台，请把流量分我点。这在云原生时代是不可能的。
*   **无负载均衡**：无法动态管理服务器集群，导致有的服务器累死，有的服务器闲死。

### 2. Zookeeper 在项目中的本质与作用

**本质**：它是一个**分布式的、具有监听机制的文件系统**。

*   **目录树结构**：我们在 ZK 上建立类似 `/UserServiceRpc/Login` 的路径。
*   **临时节点 (Ephemeral)**：这是最重要的特性。服务端注册的是临时节点，**心跳一断，节点自动删除**。
*   **作用**：
    *   **服务注册**：Provider 启动时，去 ZK “挂个号”，告诉大家我的位置。
    *   **服务发现**：Consumer 调用前，去 ZK “查个号”，动态获取最新的地址。

### 3. 环境准备：必须要先启动什么？

在运行你的 RPC 程序之前，必须**先启动 Zookeeper 的服务端进程**（通常是执行 `zkServer.sh start`）。

*   **类与控制台的关系**： C++ 代码（`ZkClient`）和 ZK 自带的控制台工具（`zkCli.sh`）地位是平等的。
*   **并不是模拟输入**：代码并不是把命令“输入”到控制台。代码是直接通过 **Zookeeper 二进制网络协议**（默认 2181 端口）与 ZK 服务器进行 TCP 通信。在控制台用 `ls /` 看到的，和在代码里用 `GetData` 查到的，是同一份内存数据。

---

### 4. 代码核心机制梳理（了解,后期再深究）

 `ZkClient` 源码有三个关键的分布式开发思想：

#### A. 异步转同步的“信号量”机制

*   **背景**：`zookeeper_init` 是**异步**的。调用完它，连接还没建立成功，它就直接返回了。
*   **代码精髓**：在 `Start()` 里定义了 `sem_t sem`。
    1. 主线程调用 `zookeeper_init` 后直接 `sem_wait(&sem)` 睡死。
    2. ZK 客户端底层线程去连服务器，连上后触发 `global_watcher`。
    3. `global_watcher` 确认连接成功，执行 `sem_post(&sem)` 唤醒。
*   **意义**：保证了框架在执行 `Create` 或 `GetData` 前，网络连接一定是 100% 就绪的。

#### B. 三线程模型（SDK 隐藏的秘密）

要理解 `ZkClient` 运行起来后，实际上有三个线程在协作：

1.  **API 调用线程**：你的主程序，调用 `Create`、`GetData`。
2.  **网络 I/O 线程**：负责发心跳包、发请求、接响应（用的是 `poll` 机制）。
3.  **Watcher 回调线程**：专门执行像 `global_watcher` 这样的函数，处理服务器发来的通知。

#### C. 节点类型的选择

在 `Create` 函数中：

*   **服务路径（如 `/FriendServiceRpc`）**：创建为**永久节点**（`state=0`）。因为它是目录，即使一个 Provider 下线了，这个分类还得留着。
*   **方法路径（如 `/FriendServiceRpc/GetFriendsList`）**：创建为**临时节点**（`state=ZOO_EPHEMERAL`）。
    *   *逻辑*：一旦该 RPC 服务器进程崩溃，ZK 感知到心跳消失，会自动删除这个节点。这就实现了**“失效自动下线”**。

---

### 5. 总结：ZooKeeperUtil 的操作流

1.  **Start**：建立 TCP 连接，利用信号量将异步连接同步化。
2.  **Create**：
    *   Provider 专用。
    *   先 `zoo_exists` 查一下，没有再 `zoo_create`。
    *   把 `ip:port` 作为数据存入节点。
3.  **GetData**：
    *   Consumer 专用。
    *   传入路径，拿到 `ip:port` 字符串，返回给 `MprpcChannel` 去建立连接。



## 第六部分：异步日志系统 (Logger)

在高性能的分布式 RPC 框架中，日志系统不仅是“记录信息”的工具，更是**保证系统吞吐量**的关键组件。

---

### 1. 核心痛点：磁盘 I/O 的阻塞问题

*   **性能瓶颈**：磁盘 I/O 的速度比内存操作慢几个数量级。如果我们的 `OnMessage` 回调函数中直接进行文件写操作（`fopen/fputs`），那么处理网络请求的 **Muduo 事件循环线程** 就会被阻塞。
*   **后果**：在写磁盘的几毫秒内，服务器无法接收和处理任何新请求。在高并发场景下，这会导致请求堆积、响应超时，框架性能大幅下降。

---

### 2. 设计方案：异步生产者-消费者模型

为了解决上述问题，项目引入了**基于线程安全队列的异步日志系统**。

*   **生产者（业务线程/工作线程）**：当需要打印日志时，业务线程只需要将日志内容和级别封装成 `LogMsg` 结构体，`Push` 到内存中的 `LockQueue` 即可。这个操作是内存级别的，速度极快，完成后线程立刻返回继续处理业务。
*   **中转站（LockQueue）**：一个带锁的模板循环队列，作为缓冲区，通过 **互斥锁 (`std::mutex`)** 保证多线程安全，通过 **条件变量 (`std::condition_variable`)** 实现线程间的同步。
*   **消费者（日志后台线程）**：在 `Logger` 初始化时，专门开启一个后台线程。它唯一的任务就是循环从 `LockQueue` 中 `Pop` 日志，并将其真正写入磁盘文件。

---

### 3. 核心代码模块解析

#### A. LockQueue

这是典型的**生产者-消费者同步机制**实现：

*   **`Push`**：加锁，将数据放入队列，然后调用 `notify_one()`。这就像是“敲铃”，告诉正在睡觉的后台线程：“有货了，快醒醒”。
*   **`Pop`**：使用 `unique_lock` 配合 `m_condvariable.wait()`。
    *   如果队列为空，后台线程会**阻塞并释放锁**，不消耗 CPU 资源。
    *   一旦收到 `notify` 且队列不为空，线程被唤醒，重新拿锁，取数据。

#### B. LogMsg 结构体（属性绑定）

*   **作用**：将日志的“级别（Level）”和“内容（Msg）”打包在一起。
*   **意义**：解决了异步环境下全局状态被覆盖的问题。每条日志在进入队列时，它的身份信息（是 INFO 还是 ERROR）就已经确定了。

#### C. Logger 单例类

*   **单例模式**：保证全进程只有一个日志实例，方便在任何地方通过 `LOG_INFO` 等宏直接调用。
*   **后台线程 (`writeLogTask`)**：
    *   使用 `std::thread` 创建，并调用 `detach()` 分离。这样日志线程会独立于主线程在后台运行。
    *   **动态文件名**：每天自动生成新的文件名（如 `2026-06-19-log.txt`），方便运维检索。
    *   **格式化处理**：自动添加 `16:30:05 => ` 这种高精度时间戳，增强可读性。

#### D. 日志宏（LOG_INFO/LOG_ERR）

*   **作用**：使用宏封装 `snprintf` 格式化逻辑。
*   **便利性**：让用户像使用 `printf` 一样方便（支持占位符），同时屏蔽了复杂的底层队列操作。

---

### 4. 总结：日志系统是如何运作的？

1.  **业务触发**：开发者在代码中写下 `LOG_INFO("connect success")`。
2.  **快速入队**：宏将字符串格式化后，调用 `Logger::Log`。`Log` 函数将消息丢进 `LockQueue`。主线程任务完成，继续处理下一个 RPC 请求。
3.  **后台等待**：后台日志线程原本在 `Pop` 处休眠。
4.  **唤醒写入**：由于 `Push` 触发了信号，后台线程苏醒，拿到消息。
5.  **落地磁盘**：后台线程获取系统日期，打开文件，写入时间戳、级别标签和内容，最后 `fclose`。

### 5. 为什么这个设计？

*   **高并发支持**：主线程几乎不参与耗时的磁盘操作。
*   **资源利用率**：利用条件变量，让后台线程在没日志时完全不占 CPU，在有日志时又能及时处理。
*   **稳定性**：即使磁盘写入瞬间变慢，也只会让 `LockQueue` 积压，而不会直接卡死 RPC 业务流程。





# 三、 讨论 Protobuf 在框架中的作用

### 1. Protobuf 是什么？

Protobuf 是 Google 开发的一种语言无关、平台无关的可扩展序列化结构数据格式。在 RPC 框架中，它解决了两个核心问题：

*   **怎么传数据？**（高效的二进制序列化，比 JSON/XML 空间更小、速度更快）。
*   **传什么数据？**（通过 `.proto` 文件定义通信合同/契约）。

---

### 2. 深度分析：Stub 与 RpcChannel 的逻辑闭环

为什么 Protobuf 产生的代码看起来如此“弯弯绕”？

#### (1) 逻辑关系图解

```text
[业务代码] -> [Stub (存根)] -> [RpcChannel (通道)] -> [网络传输]
```

*   **Stub (存根)**：它是客户端的“代言人”。你在 `.proto` 里定义了 `GetFriendsList`，生成的 `Stub` 类里就有这个同名函数。但这个函数里**没有业务逻辑**。
*   **RpcChannel**：它是框架的“物流中心”。它只有一个纯虚函数 `CallMethod`。

#### (2) 为什么设计得这么复杂？（核心：解耦）

让我们看一段伪代码分析：

```cpp
// Protobuf 自动生成的代码片段
void FriendServiceRpc_Stub::GetFriendsList(controller, request, response, done) {
    // 逻辑只有一行：调用你传入的那个 channel
    channel_->CallMethod(method_descriptor, controller, request, response, done);
}
```

**这么设计的精妙之处在于：**

1.  **类型安全（Stub 的功劳）**：当你调用 `stub.GetFriendsList` 时，编译器会检查你的参数对不对。Stub 屏蔽了底层复杂的网络细节，让调用远程方法像调用本地方法一样自然。
2.  **屏蔽多变性（Channel 的功劳）**：Protobuf 只负责生成“调用的动作（Stub）”，而“怎么发出去（Channel）”由框架开发者实现。
    *   如果你想用 TCP 发送，你就写一个继承 `RpcChannel` 的类。
    *   如果你想改用 HTTP、甚至用打孔卡片发送，你只需要再写一个类，**而业务代码和 Stub 代码一行都不用改！**

**结论**：这种设计实现了 **“业务定义”与“底层传输”的完全分离**。

---

### 3. 基于反射的动态调度

在服务端（`RpcProvider`），Protobuf 的作用更加强大，它实现了**“泛化调用”**。

#### 代码分析：

```cpp
// RpcProvider 收到请求后，不需要写一堆 if-else
google::protobuf::Service *service = m_serviceMap[service_name].m_service;
const google::protobuf::MethodDescriptor *method = m_serviceMap[service_name].m_methodMap[method_name];

// 动态创建 Request 对象
google::protobuf::Message *request = service->GetRequestPrototype(method).New();
request->ParseFromString(args_str); // 反序列化

// 动态创建 Response 对象
google::protobuf::Message *response = service->GetResponsePrototype(method).New();

// 通过反射调用业务方法
service->CallMethod(method, controller, request, response, done);
```

**深入分析：**

*   **无需 Hardcode**：如果没有 Protobuf 的 `Descriptor`（描述符），服务端收到字符串 `"GetFriendsList"` 后，必须手动 `new GetFriendsListRequest()`。
*   **万能钥匙**：通过 `MethodDescriptor`，框架可以在**运行时**知道任何一个方法的参数类型，并动态地创建出对应的对象。这使得 `RpcProvider` 变成了一个通用的容器，可以承载任何 `.proto` 定义的服务。

---

### 4. 解决 TCP 粘包的“协议头”

在你的项目中，Protobuf 还被用来定义**协议头（RpcHeader）**。

#### 协议格式：

`[header_size (4字节)] + [RpcHeader (Protobuf数据)] + [args (Protobuf数据)]`

**为什么 Header 也要用 Protobuf？**

1.  **自描述性**：Header 里包含 `service_name` 和 `method_name`。
2.  **易扩展性**：如果以后想在 Header 里加“超时时间”或“权限校验 Token”，只需要在 `.proto` 里加个字段，不需要改底层解析逻辑，且向前兼容。

---

### 5. 总结：Protobuf 在项目中的三位一体

| 角色                    | 体现形式           | 核心作用                                         |
| :---------------------- | :----------------- | :----------------------------------------------- |
| **契约**                | `.proto` 文件      | 定义了分布式系统中唯一的通讯标准。               |
| **代理 (Stub/Channel)** | `CallMethod` 机制  | 实现了业务逻辑与网络传输的解耦。                 |
| **反射 (Reflection)**   | `Descriptor/New()` | 实现了服务端的通用请求分发，让框架支持无限扩展。 |



Protobuf 在 mprpc 中不只是一个“工具”，它是连接**静态代码生成**与**动态运行时调度**的**桥梁**。它让 C++ 这种静态语言具备了类似 Java/Go 的动态特性，从而支撑起灵活的服务注册与发现机制。






**如果您觉得本项目对您有帮助，欢迎给一个 Star ⭐！**