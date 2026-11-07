#include "TestSupport.h"
#include <clocale>
int main(int argc,char** argv){try{
    std::setlocale(LC_ALL, "");require(argc==3,"test arguments supplied");initModelTests(argv[2]);
    for(const auto& name:{L"与门",L"或门",L"非门",L"与非门",L"或非门",L"异或门",L"同或门",L"半加器",L"全加器"}){
        auto gate=component(name,10);int inputs=0,outputs=0;
        for(const auto& p:gate.pins){inputs+=p.dir==se::PinDir::Input;outputs+=p.dir==se::PinDir::Output;}
        for(int bits=0;bits<(1<<inputs);++bits){
            se::NetGraph g;std::vector<se::ComponentInstance> cs;
            for(int k=0;k<inputs;++k){cs.push_back(component(L"输入引脚",k+1));g.pinNet.push_back({k});}
            cs.push_back(gate);g.nets.resize(inputs+outputs);std::vector<int> mapping;for(int k=0;k<inputs+outputs;++k)mapping.push_back(k);g.pinNet.push_back(mapping);
            se::Simulator sim;sim.Reset(cs.size());int sum=0;
            for(int k=0;k<inputs;++k){int v=(bits>>k)&1;sum+=v;sim.SetInputValue(k,v?se::LogicValue::H:se::LogicValue::L);}
            require(sim.Evaluate(cs,g),"combinational evaluation finishes");
            const wxString n=name;int expected=n==L"与门"?sum==inputs:n==L"与非门"?sum!=inputs:n==L"或门"?sum>0:n==L"或非门"?sum==0:n==L"非门"?!sum:n==L"同或门"?!(sum%2):sum%2;
            require(sim.NetValue(inputs)==(expected?se::LogicValue::H:se::LogicValue::L),"builtin truth table output");
            if(outputs==2)require(sim.NetValue(inputs+1)==(sum>=2?se::LogicValue::H:se::LogicValue::L),"adder carry output");
        }
    }
    std::cout<<"Executed existing steady-state digital model; temporal propagation is not claimed.\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
