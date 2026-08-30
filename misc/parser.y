%skeleton "lalr1.cc"
%define api.value.type variant

%code requires {
  #include "assembler.hpp"
  #include <cstdint>
  #include <string>
  #include <variant>
  #include <vector>
}

%{
  #include <iostream>

  using namespace std;
%}

%code {
  int yylex(yy::parser::semantic_type* yylval);
}

%parse-param { Assembler &assembler }

/* Names the unexpected token and what was expected */
%define parse.error verbose

/* Value carried by tokens/nonterminals. */
%token COMMA EOL COLON DOLLAR LBRACKET RBRACKET PLUS MINUS
%token SECTION GLOBAL EXTERN WORD SKIP ASCII EQU END
%token <std::string> IDENT STRING UNKNOWN_DIRECTIVE
%token <std::uint32_t> NUMBER REGISTER CSR

%token UNKNOWN

%type <std::vector<std::string>> symbol_list
%type <InstructionOperand> operand
%type <Expression> expression
%type <short> sign


%%

/* program -> line */
program
  : %empty
  | program line
  ;

/* line -> EOL | statement EOL | statement */
line
  : EOL
  | statement EOL
  | label EOL
  | label statement EOL
  ;

/* statement -> directive | instruction */
statement
  : directive
  | instruction
  ;

/* Each directive carries its own operand shape, so wrong operand counts and
 * wrong operand types are syntax errors rather than hand-written checks.
 */
directive
  : SECTION IDENT
      { assembler.openSection($2); }
  | GLOBAL symbol_list
      { for (const std::string &symbol : $2) assembler.declareGlobal(symbol); }
  | EXTERN symbol_list
      { for (const std::string &symbol : $2) assembler.declareExtern(symbol); }
  | WORD word_list
  | SKIP NUMBER
      { assembler.emitZeros($2); }
  | ASCII STRING
      { assembler.handleAscii($2); }
  | EQU IDENT COMMA expression
      { assembler.defineEquSymbol($2, $4); }
  | END
      { assembler.endAssembly(); }
  | UNKNOWN_DIRECTIVE
      { assembler.error("unknown directive " + $1); }
  ;

symbol_list
  : IDENT
      { $$ = std::vector<std::string>{ $1 }; }
  | symbol_list COMMA IDENT
      { $$ = std::move($1); $$.push_back($3); }
  ;

/* .word emits in source order, so each item acts as it reduces and no list is built. */
word_list
  : word_item
  | word_list COMMA word_item
  ;

word_item
  : IDENT   { assembler.handleWordSymbol($1); }
  | NUMBER  { assembler.handleWordLiteral($1); }
  ;

expression
  : sign IDENT 
      {
        $$ = Expression({$2, $1});
      }
  | sign NUMBER 
      {
        // casting like this because it works for some reason
        std::uint32_t num = $2;
        $$ = Expression(static_cast<std::int32_t>($1 < 0 ? -num : num));
      }
  | expression PLUS NUMBER
      {
        $$ = std::move($1);
        $$.constant += $3;
      }
  | expression PLUS IDENT
      {
        $$ = std::move($1);
        $$.terms.push_back({$3, 1}); 
      }
  | expression MINUS NUMBER
      {
        $$ = std::move($1);
        $$.constant -= $3;
      }
  | expression MINUS IDENT
      {
        $$ = std::move($1);
        $$.terms.push_back({$3, -1}); 
      }
  ;

sign
  : PLUS
      {
        $$ = 1;
      }
  | MINUS 
      {
        $$ = -1;
      }
  | %empty
      {
        $$ = 1;
      }
;

label
  : IDENT COLON
      {
        assembler.defineLabel($1);
      }
  ;

instruction
  : IDENT                            /* halt, int, ret, iret */
      { assembler.instructionNoOperand($1); }
  | IDENT REGISTER                   /* push, pop, not */
      { assembler.instructionOneRegister($1, $2); }
  | IDENT REGISTER COMMA REGISTER    /* xchg, add, sub, mul, div, and, or, xor, shl, shr */
      { assembler.instructionTwoRegisters($1, $2, $4); }
  | IDENT CSR COMMA REGISTER         /* csrrd %csr, %gpr */
      { assembler.instructionCsrRead($1, $2, $4); }
  | IDENT REGISTER COMMA CSR         /* csrwr %gpr, %csr */
      { assembler.instructionCsrWrite($1, $2, $4); }
  | IDENT operand                    /* jmp, call */
      { assembler.instructionOneOperand($1, $2  ); }
  | IDENT operand COMMA REGISTER     /* ld <operand>, %gpr */
      { assembler.instructionLoad($1, $2, static_cast<RegIndex>($4)); }
  | IDENT REGISTER COMMA operand     /* st %gpr, <operand> */
      { assembler.instructionStore($1, static_cast<RegIndex>($2), $4); }
  | IDENT REGISTER COMMA REGISTER COMMA operand   /* beq, bne, bgt */
      { assembler.instructionConditionalBranch($1, static_cast<RegIndex>($2), static_cast<RegIndex>($4), $6); }
  ;

/* RegisterDirect is not here to avoid a reduce reduce conflict with the grammar rule for  instructions with 2 registers.
 * As such, it is handled in instructionTwoRegisters.
 */
operand
  : DOLLAR NUMBER
      { $$ = InstructionOperand{ InstructionOperandKind::ImmediateLiteral, 0, $2, "" }; }
  | DOLLAR IDENT
      { $$ = InstructionOperand{ InstructionOperandKind::ImmediateSymbol, 0, 0, $2 }; }
  | NUMBER
      { $$ = InstructionOperand{ InstructionOperandKind::MemoryLiteral, 0, $1, "" }; }
  | IDENT
      { $$ = InstructionOperand{ InstructionOperandKind::MemorySymbol, 0, 0, $1 }; }
  | LBRACKET REGISTER RBRACKET
      { $$ = InstructionOperand{ InstructionOperandKind::RegisterIndirect,
                                 static_cast<RegIndex>($2), 0, "" }; }
  | LBRACKET REGISTER PLUS NUMBER RBRACKET
      { $$ = InstructionOperand{ InstructionOperandKind::RegisterLiteralOffset,
                                 static_cast<RegIndex>($2), $4, "" }; }
  | LBRACKET REGISTER PLUS IDENT RBRACKET
      { $$ = InstructionOperand{ InstructionOperandKind::RegisterSymbolOffset,
                                 static_cast<RegIndex>($2), 0, $4 }; }
  ;

%%

void yy::parser::error(const std::string &msg) {
  assembler.error(msg);
}
