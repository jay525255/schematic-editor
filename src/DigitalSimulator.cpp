#include "DigitalSimulator.h"
#include <algorithm>
#include <limits>
#include <set>
namespace se {
wxString LogicValueText(int value) {
    if (value < 0) return "X";
    if (value == 2) return "ERR";
    return wxString::Format("%d", value);
}
wxString DigitalModel(const ComponentInstance& c) {
    if(!c.logicModel.empty()) return c.logicModel;
    static const std::map<wxString,wxString> models={
        {L"与门","and"},{L"与非门","nand"},{L"或门","or"},{L"或非门","nor"},
        {L"非门","not"},{L"异或门","xor"},{L"同或门","xnor"},
        {L"半加器","half-adder"},{L"全加器","full-adder"},
        {L"输入引脚","input"},{L"常量","input"},{L"时钟","clock"},{L"VCC","high"},{L"GND","low"},
        {L"输出引脚","observe"},{L"探针","observe"},{L"网络标签","observe"},{L"全局标签","observe"},
        {L"文本","observe"},{L"连接点","observe"},{L"未连接标记","observe"},
        // 时序元件（任务：时序逻辑）
        {L"D 触发器","dff"},{L"JK 触发器","jkff"},{L"4 位计数器","counter"},
        {L"ROM 16×4","rom"},{L"RAM 16×4","ram"}};
    auto i=models.find(c.name);if(i!=models.end())return i->second;
    // Preserve existing user-library names such as 三输入与门.
    static const std::pair<const wchar_t*, const char*> aliases[] = {
        {L"与非门","nand"},{L"或非门","nor"},{L"同或门","xnor"},{L"异或门","xor"},
        {L"与门","and"},{L"或门","or"},{L"非门","not"},{L"半加器","half-adder"},{L"全加器","full-adder"},
        // 顺序同上：先长名字，再短名字（JK 触发器要排在"触发器"之前）
        {L"JK 触发器","jkff"},{L"D 触发器","dff"},{L"触发器","dff"},
        {L"计数器","counter"},{L"ROM","rom"},{L"RAM","ram"}};
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
// ------------------------------------------------------------
// 时序元件（触发器 / 计数器 / ROM / RAM）
//
// 为什么单独一路求值：
//   组合门的输出只取决于"这一拍的输入"，EvaluateDigital 是纯函数；
//   时序元件的输出还取决于"上一拍存了什么"，所以必须在仿真器里
//   给每个元件留一块记忆（SeqState），并且只有在时钟"上升沿"
//   那一下才更新记忆。否则时钟一高，触发器会每个 tick 都重新采样。
// ------------------------------------------------------------
bool DigitalSimulator::IsSequentialModel(const wxString& model)
{
    return model == "dff" || model == "jkff" || model == "counter" || model == "rom" || model == "ram";
}

namespace {

// truthTable 在这里复用成 ROM 的"出厂内容"：16 个十六进制数字，
// 依次是地址 0..15 的字。写不出 16 个合法字符就用"地址值"当内容
// （D = A），这样用户一放上去就能看到有变化的输出。
void LoadRomImage(const wxString& image, std::vector<int>& mem)
{
    mem.assign(16, 0);
    bool ok = image.length() >= 16;
    for (size_t i = 0; ok && i < 16; ++i)
    {
        const wxChar ch = image[i];
        int digit = -1;
        if (ch >= L'0' && ch <= L'9') digit = ch - L'0';
        else if (ch >= L'A' && ch <= L'F') digit = ch - L'A' + 10;
        else if (ch >= L'a' && ch <= L'f') digit = ch - L'a' + 10;
        if (digit < 0) ok = false;
        else mem[i] = digit;
    }
    if (!ok)
        for (int i = 0; i < 16; ++i) mem[i] = i;
}

} // namespace

std::vector<int> DigitalSimulator::EvaluateSequential(size_t index, const std::vector<int>& in)
{
    const ComponentInstance& c = m_components[index];
    const wxString model = DigitalModel(c);
    SeqState& st = m_seq[index];

    std::vector<int> out;
    for (const auto& p : c.pins)
        if (p.dir == PinDir::Output) out.push_back(-1);
    if (out.empty()) return out;

    auto bit = [&in](size_t k) { return k < in.size() ? in[k] : -1; };

    if (model == "dff")
    {
        const int d = bit(0), clk = bit(1);
        if (st.lastClock == 0 && clk == 1 && (d == 0 || d == 1)) st.q = d;
        st.lastClock = clk;
        out[0] = st.q;
        if (out.size() >= 2) out[1] = 1 - st.q;
        return out;
    }

    if (model == "jkff")
    {
        const int j = bit(0), k = bit(1), clk = bit(2);
        if (st.lastClock == 0 && clk == 1 && (j == 0 || j == 1) && (k == 0 || k == 1)) {
            if (j == 1 && k == 1)      st.q = 1 - st.q;   // 翻转
            else if (j == 1)           st.q = 1;          // 置位
            else if (k == 1)           st.q = 0;          // 复位
        }
        st.lastClock = clk;
        out[0] = st.q;
        if (out.size() >= 2) out[1] = 1 - st.q;
        return out;
    }

    if (model == "counter")
    {
        const int clk = bit(0), rst = bit(1);
        if (rst == 1)            st.q = 0;                       // 复位优先，电平有效
        else if (st.lastClock == 0 && clk == 1) st.q = (st.q + 1) & 0xF;
        st.lastClock = clk;
        for (size_t k = 0; k < out.size(); ++k) out[k] = (st.q >> k) & 1;
        return out;
    }

    if (model == "rom" || model == "ram")
    {
        int  addr = 0;
        bool addrKnown = true;
        for (int k = 0; k < 4; ++k) {
            const int v = bit(static_cast<size_t>(k));
            if (v != 0 && v != 1) { addrKnown = false; break; }
            addr |= v << k;
        }

        if (model == "ram")
        {
            const int we = bit(4);
            if (st.lastWrite == 0 && we == 1 && addrKnown) {
                int data = 0; bool dataKnown = true;
                for (int k = 0; k < 4; ++k) {
                    const int v = bit(static_cast<size_t>(5 + k));
                    if (v != 0 && v != 1) { dataKnown = false; break; }
                    data |= v << k;
                }
                if (dataKnown) st.mem[addr] = data;   // 写使能上升沿写入
            }
            st.lastWrite = we;
        }

        const int word = addrKnown ? st.mem[addr] : -1;   // 读是异步的
        for (size_t k = 0; k < out.size(); ++k)
            out[k] = (word < 0) ? -1 : ((word >> k) & 1);
        return out;
    }

    return EvaluateDigital(c, in);
}

// 仿真开始那一刻先把时序元件的输出"摆出来"，否则要等第一拍才有值
void DigitalSimulator::SeedSequential(size_t index)
{
    const ComponentInstance& c = m_components[index];
    std::vector<int> unknown;
    for (const auto& p : c.pins)
        if (p.dir == PinDir::Input) unknown.push_back(-1);

    const std::vector<int> out = EvaluateSequential(index, unknown);

    size_t k = 0;
    for (size_t p = 0; p < c.pins.size(); ++p)
        if (c.pins[p].dir == PinDir::Output && k < out.size()) m_drives[index][p] = out[k++];

    m_seq[index].lastClock = -1;
    m_seq[index].lastWrite = -1;
    m_seq[index].seeded    = true;
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
        static const std::set<wxString> valid={"and","nand","or","nor","not","buffer","xor","xnor","half-adder","full-adder","input","clock","low","high","observe","truth",
            "dff","jkff","counter","rom","ram"};
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
        if(model=="dff"&&(inputs!=2||outputs!=2)){error=L"D 触发器需要 D、CLK 两个输入和 Q、QN 两个输出";return false;}
        if(model=="jkff"&&(inputs!=3||outputs!=2)){error=L"JK 触发器需要 J、K、CLK 三个输入和 Q、QN 两个输出";return false;}
        if(model=="counter"&&(inputs!=2||outputs!=4)){error=L"4 位计数器需要 CLK、RST 两个输入和 Q0~Q3 四个输出";return false;}
        if(model=="rom"&&(inputs!=4||outputs!=4)){error=L"ROM 需要 A0~A3 四个地址输入和 D0~D3 四个数据输出";return false;}
        if(model=="ram"&&(inputs!=9||outputs!=4)){error=L"RAM 需要 A0~A3、WE、D0~D3 九个输入和 DQ0~DQ3 四个输出";return false;}
        if(model=="input")m_external[c.id]=c.value=="1"?1:c.value=="X"?-1:0;
        m_drives.emplace_back(c.pins.size(),-1);m_inputs.emplace_back();m_generation.push_back(0);
        m_seq.emplace_back();
        if(model=="rom"||model=="ram"){LoadRomImage(model=="rom"?c.truthTable:wxString(),m_seq.back().mem);}
    }
    for(size_t i=0;i<m_components.size();++i)
        if(IsSequentialModel(DigitalModel(m_components[i])))SeedSequential(i);
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
        m_events.push({m_time+static_cast<unsigned>(c.delayTicks),++m_sequence,++m_generation[i],i,
                       IsSequentialModel(model)?EvaluateSequential(i,inputs):EvaluateDigital(c,inputs)});
    }
}
bool DigitalSimulator::Advance(std::uint64_t ticks){
    if(!m_ready||ticks>1000000||m_time>std::numeric_limits<std::uint64_t>::max()-ticks){m_error=L"仿真步数越界";return false;}
    while(ticks--){++m_time;ScheduleSources();Process();}return true;
}
}
