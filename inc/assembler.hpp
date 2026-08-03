#include <climits>
#include <string>
#include <vector>

struct SymTab_entry {
    /*  Symbol table entry */
    // Constants and types
    static constexpr unsigned short UND = 0;
    static constexpr unsigned short ABS = USHRT_MAX; // just gotta care not to hit this value but that shouldnt be a problem
    enum class Type {NOTYP, SCTN};
    enum class Bind {LOC, GLOB};
    // Fields: Num Value Size Type Bind Ndx Name
    unsigned int num; 
    unsigned int value;
    unsigned int size;
    Type type;  
    Bind bind;
    unsigned short ndx;
    std::string name;
};

class Assembler {
public:
    Assembler() {

    }
    
private:
    std::vector<SymTab_entry> symTab;
};
