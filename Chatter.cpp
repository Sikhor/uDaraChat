#include "Chatter.h"

std::string Chatter::GetRandomMessage() 
{
      static std::vector<std::string> messages = {
        "Why do you think Dara is that corrupted?",
        "Tank looking for work!",
        "DPS looking for work! Willing to travel.",
        "Looking for WaterBastion update",
        "Why me?",
        "Really?",
        "I just got here!",
        "I'm not paid enough for this.",
        "This isn't in my contract!",
        "Again?!",
        "Great. Just great.",
        "Is that thing supposed to glow?",
        "Nope. I'm out.",
        "What did I just step in?",
        "I think it's watching me...",
        "This planet gives me the creeps.",
        "Who's bright idea was this?",
        "I thought this was a vacation mission.",
        "I left the oven on back home.",
        "If I die, tell my clone I said hi.",
        "I demand hazard pay!",
        "Aliens again? Ugh.",
        "Not the face!",
        "I've got a bad feeling about this..."
        "How's it going?",
        "Just another day in paradise.",
        "Anyone up for a raid?",
        "Looking for group!",
        "Is this thing on?",
        "Can you hear me now?",
        "Testing, testing, 1-2-3.",
        "I love this game!",
        "Where's the loot?",
        "Let's get this party started!",
        "Anyone need a healer?",
        "I'm just here for the snacks.",
        "Did you see that boss fight?",
        "This is my favorite zone.",
        "Who's got the map?"
         };
        return GetRandomMessage(messages);
}

std::string Chatter::GetRandomName() 
{
      static std::vector<std::string> messages = {
        "AutoClient_X99",
        "ChatProbe_007",
        "NullEcho",
        "ByteSniffer",
        "PingLord",
        "CaptainSpam",
        "DaraBot9000",
        "AFKCommander",
        "SyntaxTerror",
        "PacketJockey",
        "WhisperWarden",
        "MessageMancer",
        "Talkonaut",
        "CyberGoblin",
        "EchoSpecter",
        "NetGremlin",
        "QueueHopper",
        "Botzilla",
        "IdleWhisper",
        "GhostInTheChat"
    };

    return GetRandomMessage(messages);
}

std::string GenerateHashedPassword(const std::string& passphrase)
{
    unsigned char hash[SHA_DIGEST_LENGTH]; // SHA-1 is 20 bytes

    SHA1(reinterpret_cast<const unsigned char*>(passphrase.c_str()), passphrase.size(), hash);

    std::ostringstream oss;
    for (int i = 0; i < SHA_DIGEST_LENGTH; ++i)
    {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }

    return oss.str();
}


std::string Chatter::GetRandomMessage(const std::vector<std::string>& messages) {
    if (messages.empty()) {
        return("empty");
    }

    // Static: initialized once, reused on every call
    static std::random_device rd;
    static std::mt19937 gen(rd());

    std::uniform_int_distribution<std::size_t> dist(0, messages.size() - 1);

    return messages[dist(gen)];
}


Chatter::Chatter()
{
  MyCharName= GetRandomName();
  MyZoneName= "Dungeon";
  MyGroupId= "";
  MyPass= GenerateHashedPassword(MyCharName);
}

void Chatter::SetCharName(std::string name)
{
  MyCharName= name;
  MyPass= GenerateHashedPassword(MyCharName);
}

void Chatter::AddChatPartner(std::string name)
{
  MyChatPartners.push_back(name);
}

std::string Chatter::GetRandomChatPartner() 
{
    return GetRandomMessage(MyChatPartners);
}

std::string Chatter::GetConnectionString()
{  std::string line;
  return "connect:name="+MyCharName+ ";zone=Dungeon;group=7;pass="+MyPass;
}

std::string Chatter::GetSubscribeString1()
{
  return "subscribe:World";   
}
std::string Chatter::GetSubscribeString2()
{
  return "subscribe:SomeChannel";   
} 
std::string Chatter::GetSubscribeString3()
{
  std::string line;
  return "subscribe:" + MyZoneName;
}


std::string Chatter::GetRandomChatMsg()
{
  FDaraChatMsg ChatMsg;
  ChatMsg.ChatType= "Channel";
  ChatMsg.ChatCmdType= "None";
  ChatMsg.Sender= MyCharName;
  ChatMsg.Recipient= "World";
  ChatMsg.Msg= GetRandomMessage();
  
  return ChatMsg.SerializeToSend();
}

std::string Chatter::GetRandomZoneMsg()
{
  FDaraChatMsg ChatMsg;
  ChatMsg.ChatType= "Zone";
  ChatMsg.ChatCmdType= "None";
  ChatMsg.Sender= MyCharName;
  ChatMsg.Recipient= "Dungeon";
  ChatMsg.Msg= GetRandomMessage();
  
  return ChatMsg.SerializeToSend();
}

std::string Chatter::GetRandomTellMsg()
{
  FDaraChatMsg ChatMsg;
  ChatMsg.ChatType= "Tell";
  ChatMsg.ChatCmdType= "None";
  ChatMsg.Sender= MyCharName;
  ChatMsg.Recipient= GetRandomChatPartner();
  ChatMsg.Msg= GetRandomMessage();
  
  return ChatMsg.SerializeToSend();
}

std::string Chatter::GetRandomGroupMsg()
{
  FDaraChatMsg ChatMsg;
  ChatMsg.ChatType= "Group";
  ChatMsg.ChatCmdType= "None";
  ChatMsg.Sender= MyCharName;
  ChatMsg.Recipient= "Group";
  ChatMsg.Msg= GetRandomMessage();
  
  return ChatMsg.SerializeToSend();
}


std::string Chatter::GenerateRandomLogin()
{
  MyCharName= GetRandomName();
  MyZoneName= "Dungeon";
  MyGroupId= "";
  MyPass= GenerateHashedPassword(MyCharName);
  std::string line="connect:name="+MyCharName+ ";zone=Alpha;group=7;pass="+MyPass;
  return line;
}
   

std::string Chatter::GetRandomInvite()
{
    FDaraChatMsg ChatMsg;
    ChatMsg.ChatType= "Cmd";
    ChatMsg.ChatCmdType= "GroupInvite";
    ChatMsg.Sender= MyCharName;
    ChatMsg.Recipient= GetRandomChatPartner();
    ChatMsg.Msg= "";

    return ChatMsg.SerializeToSend();
}

void Chatter::SendRandomZoneMessage()
{
  SendZoneMessage(GetRandomZoneMsg());
}


void Chatter::SendZoneMessage(std::string msg)
{
  static int count=0;

  count++;
  if(count>10){
    count=0;
  }
  if(count==5){
    FDaraChatMsg ChatMsg;
    ChatMsg.ChatType= "Zone";
    ChatMsg.ChatCmdType= "None";
    ChatMsg.Sender= MyCharName;
    ChatMsg.Recipient= MyZoneName;
    ChatMsg.Msg= msg;
  } 
  return;
}

void Chatter::SendTellMessage()
{
  static int count=0;

  count++;
  if(count>5){
    count=0;
  }
  if(count==5){
    FDaraChatMsg ChatMsg;
    ChatMsg.ChatType= "Tell";
    ChatMsg.ChatCmdType= "None";
    ChatMsg.Sender= MyCharName;
    ChatMsg.Recipient= "MeMercenary";
    ChatMsg.Msg= GetRandomMessage();
  } 
  return;
}


