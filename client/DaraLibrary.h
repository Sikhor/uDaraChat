#pragma once
#include <atomic>
#include <signal.h>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <string>
#include <map>
#include <chrono>


struct FDaraChatMsg
{
    std::string ChatType;
    std::string Sender;
    std::string Recipient;
    std::string Msg;
};

