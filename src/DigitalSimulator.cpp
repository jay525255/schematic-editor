#include "DigitalSimulator.h"
#include <algorithm>
#include <limits>
#include <set>
namespace se {
wxString LogicValueText(int value) {return value<0?"X":value==2?"ERR":wxString::Format("%d",value);}
wxString DigitalModel(const ComponentInstance& c) {
    if(!c.logicModel.empty()) return c.logicModel;
    static const std::map<wxString,wxString> models={
        {L"与门","and"},{L"与非门","nand"},{L"或门","or"},{L"或非门","nor"},
        {L"非门","not"},{L"异或门","xor"},{L"同或门","xnor"},
        {L"半加器","half-adder"},{L"全加器","full-adder"},
        {L"输入引脚","input"},{L"常量","input"},{L"时钟","clock"},{L"VCC","high"},{L"GND","low"},
        {L"输出引脚","observe"},{L"探针","observe"},{L"网络标签","observe"},{L"全局标签","observe"},
        {L"文本","observe"},{L"连接点","observe"},{L"未连接标记","observe"}};
    auto i=models.find(c.name);if(i!=models.end())return i->second;
    // Preserve existing user-library names such as 三输入与门.
    static const std::pair<const wchar_t*, const char*> aliases[] = {
        {L"与非门","nand"},{L"或非门","nor"},{L"同或门","xnor"},{L"异或门","xor"},
        {L"与门","and"},{L"或门","or"},{L"非门","not"},{L"半加器","half-adder"},{L"全加器","full-adder"}};
    for(const auto& alias:aliases)if(c.name.Contains(alias.first))return alias.second;
    return {};

}
std::vector<int> EvaluateDigital(const ComponentInstance& c,const std::vector<int>& in) {
    const auto model=DigitalModel(c);size_t outputs=0;
    for(const auto& p:c.pins)if(p.dir==PinDir::Output)++outputs;
    std::vector<int> result(outputs,-1);
    if(in.empty())return result;
    bool known=std::all_of(in.begin(),in.end(),[](int v){return v==0||v==1;});
    if(std::find(in.begin(),in.end(),2)!=in.end()){std::fill(result.begin(),result.end(),2);return result;}
    int value=-1,sum=0;for(int v:in)if(v>0)sum+=v;
    if(model=="and"||model=="nand"){
        if(std::find(in.begin(),in.end(),0)!=in.end())value=0;else if(known)value=1;
        if(model=="nand"&&value>=0)value=1-value;
    }else if(model=="or"||model=="nor"){
        if(std::find(in.begin(),in.end(),1)!=in.end())value=1;else if(known)value=0;
        if(model=="nor"&&value>=0)value=1-value;
    }else if(known){
        if(model=="not")value=1-in[0];
        else if(model=="buffer")value=in[0];
        else if(model=="xor"||model=="xnor"||model=="half-adder"||model=="full-adder")value=(sum%2)^(model=="xnor");
        else if(model=="truth"&&in.size()<=8){
            size_t row=0;for(size_t k=0;k<in.size();++k)row|=size_t(in[k])<<k;
            if(c.truthTable.size()==(size_t(1)<<in.size())*outputs)
                for(size_t k=0;k<outputs;++k)result[k]=c.truthTable[row*outputs+k]=='1'?1:0;
            return result;
        }
    }
    std::fill(result.begin(),result.end(),value);
    if((model=="half-adder"||model=="full-adder")&&outputs==2&&known)result[1]=sum>=2?1:0;
    return result;
}
bool DigitalSimulator::Reset(const std::vector<ComponentInstance>& cs,const NetGraph& graph,wxString& error){
    *this=DigitalSimulator();m_components=cs;m_graph=graph;m_values.assign(graph.nets.size(),-1);
    if(graph.pinNet.size()!=cs.size()){error=L"网络映射与元件数量不一致";return false;}
    std::set<int> ids;
    for(size_t i=0;i<cs.size();++i){
        const auto& c=cs[i];const auto model=DigitalModel(c);
        if(c.id<=0||!ids.insert(c.id).second){error=L"元件ID必须为唯一正整数";return false;}
        if(model.empty()){error=L"没有数字模型："+c.refdes+" "+c.name;return false;}
        if(graph.pinNet[i].size()!=c.pins.size()){error=L"引脚网络映射不完整";return false;}
        for(int net:graph.pinNet[i])if(net<0||net>=static_cast<int>(graph.nets.size())){error=L"非法引脚网络下标";return false;}
        int inputs=0,outputs=0;for(const auto& p:c.pins){inputs+=p.dir==PinDir::Input;outputs+=p.dir==PinDir::Output;}
        if(c.delayTicks<1||c.delayTicks>1000||c.clockPeriodTicks<2||c.clockPeriodTicks>10000||c.clockPeriodTicks%2){error=L"传播延迟或时钟周期非法";return false;}
        static const std::set<wxString> valid={"and","nand","or","nor","not","buffer","xor","xnor","half-adder","full-adder","input","clock","low","high","observe","truth"};
        if(!valid.count(model)){error=L"未知数字模型："+model;return false;}
        if((model=="not"||model=="buffer")&&(inputs!=1||outputs!=1)){error=L"反相/缓冲器必须为一入一出";return false;}
        if((model=="input"||model=="clock")&&(inputs!=0||outputs!=1)){error=L"输入源必须为零输入单输出";return false;}
        if(model=="input"&&!c.value.empty()&&c.value!="0"&&c.value!="1"&&c.value!="X"){error=L"数字输入只能为0、1或X";return false;}
        if((model=="half-adder"&&(inputs!=2||outputs!=2))||(model=="full-adder"&&(inputs!=3||outputs!=2))){error=L"加法器引脚数量非法";return false;}
        if(model!="observe"&&model!="low"&&model!="high"&&model!="input"&&model!="clock"&&(inputs<1||outputs<1)){error=L"逻辑门需要输入和输出引脚";return false;}
        if(model=="truth"){
            if(inputs<1||inputs>8||outputs<1||outputs>16||c.truthTable.size()!=(size_t(1)<<inputs)*outputs){error=L"自定义真值表长度与输入输出数量不匹配";return false;}
            for(auto bit:c.truthTable)if(bit!='0'&&bit!='1'){error=L"真值表只能包含0和1";return false;}
        }
        if(model=="input")m_external[c.id]=c.value=="1"?1:c.value=="X"?-1:0;
        m_drives.emplace_back(c.pins.size(),-1);m_inputs.emplace_back();m_generation.push_back(0);
    }
    m_ready=true;ScheduleSources();Process();error.clear();return true;
}
bool DigitalSimulator::SetInput(int id,int value){
    auto it=m_external.find(id);if(!m_ready||it==m_external.end()||value<-1||value>1)return false;
    it->second=value;ScheduleSources();Process();return true;
}
int DigitalSimulator::InputValue(int id)const{auto i=m_external.find(id);return i==m_external.end()?-1:i->second;}
int DigitalSimulator::Value(int net)const{return net>=0&&net<static_cast<int>(m_values.size())?m_values[net]:-1;}
void DigitalSimulator::ScheduleSources(){
    for(size_t i=0;i<m_components.size();++i){const auto& c=m_components[i];auto model=DigitalModel(c);int value=-1;
        if(model=="input")value=m_external[c.id];
        else if(model=="clock")value=(m_time/(c.clockPeriodTicks/2))%2;
        else if(model=="low")value=0;else if(model=="high")value=1;else continue;
        for(size_t k=0;k<c.pins.size();++k)if(c.pins[k].dir==PinDir::Output||c.pins[k].dir==PinDir::Passive)m_drives[i][k]=value;
    }
}
void DigitalSimulator::Resolve(){
    std::vector<std::vector<int>> drivers(m_values.size());
    for(size_t i=0;i<m_components.size();++i){const auto& c=m_components[i];auto model=DigitalModel(c);
        if(model=="observe")continue;
        for(size_t k=0;k<c.pins.size();++k)if(c.pins[k].dir==PinDir::Output||((model=="low"||model=="high")&&c.pins[k].dir==PinDir::Passive))drivers[m_graph.pinNet[i][k]].push_back(m_drives[i][k]);
    }
    for(size_t n=0;n<drivers.size();++n){int value=-1;bool unknown=false;
        for(int drive:drivers[n]){if(drive<0){unknown=true;continue;}if(value<0)value=drive;else if(value!=drive)value=2;}
        if(value!=2&&unknown)value=-1;
        if(value!=m_values[n]){++m_transitions;m_values[n]=value;}
    }
}
void DigitalSimulator::Process(){
    while(!m_events.empty()&&m_events.top().at<=m_time){auto event=m_events.top();m_events.pop();
        if(event.generation!=m_generation[event.component])continue;
        size_t out=0;const auto& pins=m_components[event.component].pins;
        for(size_t k=0;k<pins.size();++k)if(pins[k].dir==PinDir::Output)m_drives[event.component][k]=event.outputs[out++];
    }
    Resolve();
    for(size_t i=0;i<m_components.size();++i){const auto& c=m_components[i];auto model=DigitalModel(c);
        if(model=="input"||model=="clock"||model=="low"||model=="high"||model=="observe")continue;
        std::vector<int> inputs;for(size_t k=0;k<c.pins.size();++k)if(c.pins[k].dir==PinDir::Input)inputs.push_back(Value(m_graph.pinNet[i][k]));
        if(inputs==m_inputs[i])continue;m_inputs[i]=inputs;
        m_events.push({m_time+static_cast<unsigned>(c.delayTicks),++m_sequence,++m_generation[i],i,EvaluateDigital(c,inputs)});
    }
}
bool DigitalSimulator::Advance(std::uint64_t ticks){
    if(!m_ready||ticks>1000000||m_time>std::numeric_limits<std::uint64_t>::max()-ticks){m_error=L"仿真步数越界";return false;}
    while(ticks--){++m_time;ScheduleSources();Process();}return true;
}
}
