#pragma once

// mprpc 框架的基础类，设置为单例

class MprpcApplication
{
public:
    static void Init(int argc,char **argv);
    static  MprpcApplication& GetInstance();
    MprpcApplication();
    MprpcApplication(const MprpcApplication&) = delete;
    MprpcApplication(MprpcApplication&&) = delete;
};