#include "usp_spotify_tokens.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <new>
using namespace usp_spotify_tokens;
const std::string GOOD=R"({"access_token":"access_A+/==","token_type":"Bearer","expires_in":3600,"refresh_token":"refresh+/%&=","scope":"user-read-playback-state"})";

bool parse(std::string_view body,Reply *reply) {return ReplyParser(body).parse(reply);}

void reply_cases() {
  Reply reply;assert(parse(GOOD,&reply) && reply.success());
  assert(std::string(reply.refresh.data())=="refresh+/%&=");
  const std::string alias=R"({"\u0061ccess_token":"x","token_type":"Bearer","expires_in":1})";
  assert(parse(alias,&reply) && reply.access[0]=='x');
  for(const std::string &bad:std::vector<std::string>{
      "", "null","[]","{}",GOOD+"x",
      R"({"access_token":"x","access_token":"y","token_type":"Bearer","expires_in":1})",
      R"({"access_token":"x","\u0061ccess_token":"y","token_type":"Bearer","expires_in":1})",
      R"({"access_token":"x","token_type":"Bearer","expires_in":01})",
      R"({"access_token":"x","token_type":"Bearer","expires_in":0})",
      R"({"access_token":"x","token_type":"Bearer","expires_in":86401})",
      R"({"access_token":"x","token_type":"Bearer","expires_in":true})",
      R"({"access_token":"x","token_type":"Bearer","expires_in":"3600"})",
      R"({"access_token":"x","token_type":"Bearer","expires_in":1.5})",
      R"({"access_token":"x","token_type":"Bearer","expires_in":1e3})",
      R"({"access_token":"x","token_type":"Basic","expires_in":1})",
      R"({"access_token":"x\r\nheader","token_type":"Bearer","expires_in":1})",
      R"({"access_token":"x\u0000","token_type":"Bearer","expires_in":1})",
      R"({"access_token":"x","token_type":"Bearer","expires_in":1,"error":"invalid_grant"})",
      R"({"access_token":"x","token_type":"Bearer","expires_in":1,"error_description":"bad"})",
      R"({"access_token":"x","token_type":"Bearer","expires_in":1,})",
      R"({"access_token":"x","token_type":"Bearer","expires_in":1,"new":{}})"}) {
    assert(!parse(bad,&reply) && reply.access[0]==0 && reply.refresh[0]==0);
  }
  assert(!parse(GOOD+std::string(1,'\0'),&reply));
  assert(parse(" \r\n"+GOOD+"\t ",&reply));
  assert(parse(R"({"error":"invalid_grant","error_description":"Expired"})",&reply));
  assert(!reply.success() && std::string(reply.error.data())=="invalid_grant");
  assert(!parse(std::string(MAX_BODY+1,' '),&reply));
}

void limits() {
  Reply reply;
  auto body=[](size_t count){return std::string(R"({"access_token":")")+
      std::string(count,'x')+R"(","token_type":"Bearer","expires_in":1})";};
  assert(parse(body(MAX_TOKEN),&reply));
  assert(!parse(body(MAX_TOKEN+1),&reply));
  assert(!parse(R"({"access_token":"a=b","token_type":"Bearer","expires_in":1})",&reply));
  assert(!parse(R"({"access_token":"x y","token_type":"Bearer","expires_in":1})",&reply));
  assert(!parse(R"({"access_token":"x","token_type":"Bearer","expires_in":1,"refresh_token":""})",&reply));
  reply.access.fill('x');reply.fields=7;reply.expires_seconds=1;assert(!reply.success());
}

void lifecycle() {
  Reply reply;Store store;
  assert(parse(GOOD,&reply));
  assert(store.install(reply,"client123",100,200,false));
  assert(store.valid(3599999+100) && !store.valid(3600000+100));
  assert(!store.refresh_due(100) && store.refresh_due(3540100));
  assert(store.refresh()=="refresh+/%&=");
  assert(parse(R"({"access_token":"new","token_type":"Bearer","expires_in":3600})",&reply));
  assert(!store.install(reply,"other",1000,1001,true));
  assert(store.install(reply,"client123",1000,1001,true));
  assert(store.refresh()=="refresh+/%&=" && store.scope()=="user-read-playback-state");
  assert(store.access(1002)=="new");
  Store empty;assert(!empty.install(reply,"client123",1000,1001,false));
  assert(parse(GOOD,&reply));
  assert(!store.install(reply,"client123",1000,3601000,true));
  assert(store.access(1002)=="new");
  assert(store.install(reply,"client123",UINT32_MAX-500,50,false));
  assert(store.valid(50) && !store.valid(uint32_t(UINT32_MAX-500+3600000U)));
  store.clear();assert(!store.valid(0) && store.refresh().empty() && store.access(0).empty());
  reply.clear();assert(reply.access[0]==0 && reply.refresh[0]==0 && !reply.success());
}

void forms() {
  usp_spotify_auth_return::TokenRequest request;Form form;
  usp_spotify_auth_return::copy_field(request.code,"a+/%&= ");
  usp_spotify_auth_return::copy_field(request.verifier,std::string(43,'a'));
  usp_spotify_auth_return::copy_field(request.client_id,"client123");
  usp_spotify_auth_return::copy_field(request.redirect_uri,"https://return.test/cb");
  assert(code_form(request,&form));
  const std::string encoded(form.data(),form.size());
  assert(encoded.find("code=a%2B%2F%25%26%3D%20")!=std::string::npos);
  assert(encoded.find("client_secret")==std::string::npos);
  assert(refresh_form("client123","a+/%&=",&form));
  assert(std::string(form.data()).find("refresh_token=a%2B%2F%25%26%3D")!=std::string::npos);
  assert(!refresh_form("client123","bad\nvalue",&form) && form.size()==0);
  assert(!form.append("overflow",std::string(MAX_FORM,'+')) && form.size()==0);
  request.code.fill('x');assert(!code_form(request,&form) && form.size()==0);
  form.clear();assert(form.data()[0]==0);
}

/** Malformed bytes never leave partially accepted access or refresh values. */
void mutations() {
  std::mt19937 random(0x51504b43U);
  Reply reply;
  for(int i=0;i<5000;++i) {
    std::string body=GOOD;
    const auto at=size_t(random()%body.size());
    if(i%3==0)body.erase(at,1);
    else if(i%3==1)body[at]=char(random()%256);
    else body.insert(at,1,char(random()%256));
    const bool accepted=parse(body,&reply);
    if(accepted)assert(reply.success() || reply.error[0]);
    else assert(reply.access[0]==0 && reply.refresh[0]==0 && reply.fields==0);
  }
}

/** Destruction erases known token bytes even when the storage remains allocated. */
void destructor_erases() {
  alignas(Store) std::array<unsigned char,sizeof(Store)> storage{};
  Reply reply;assert(parse(GOOD,&reply));
  Store *store=new(storage.data()) Store();
  assert(store->install(reply,"client123",0,1,false));
  store->~Store();
  const std::string access="access_A+/==",refresh="refresh+/%&=";
  assert(std::search(storage.begin(),storage.end(),access.begin(),access.end())==storage.end());
  assert(std::search(storage.begin(),storage.end(),refresh.begin(),refresh.end())==storage.end());
}

int main() {
  reply_cases();limits();lifecycle();forms();mutations();destructor_erases();
  std::cout<<"SPOTIFY_TOKENS_PREFLIGHT=PASS strict_json_duplicates_bounds_types_erase forms lifecycle_refresh_no_rotation wrap mutations5000 destructor_zeroize no_network\n";
}
