%skeleton "lalr1.cc"
%define api.value.type variant

%code requires {
  #include "assembler.hpp"
  #include <cstdint>
  #include <string>
  #include <variant>
  #include <vector>
  #include <algorithm>

  using Operand = std::variant<std::uint32_t, std::string>;
  using OperandList = std::vector<Operand>;
}

%{
  #include <iostream>

  using namespace std;
%}

%code {
  int yylex(yy::parser::semantic_type* yylval);
}

%parse-param { Assembler &assembler }

/* Value carried by tokens/nonterminals. */
%token DOT COMMA EOL COLON
%token <std::string> IDENT STRING
%token <std::uint32_t> NUMBER

%type <OperandList> operand_list

%%

/* program -> line */
program
  : /* empty */
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

/* directive -> '.' IDENT IDENT   (.section NAME)
 *            | '.' IDENT         (.end)
 * We keep the lexer dumb (just DOT + IDENT) and decide *which* directive
 * here by looking at the name. Adding .global/.word/... = another branch.
 * NOTE: NECEMO TRETIRATI global, word, extern, kao REZERVISANE RECI, pa ih lekser ne moze slati kao tokene
 * kao u PP projektu, a isto tako necemo to raditi ni za MNEMONIKE INSTRUKCIJA, tako da ce sve da se kupi
 * preko identa verovatno 
 */
directive
  : DOT IDENT
      {
        if ($2 == "end") {
          assembler.endAssembly();
        }
        else {
          error("unknown directive ." + $2);
        }
      }
  | DOT IDENT operand_list
      {
        if ($2 == "section") {
          if($3.size() != 1)
            error("section directive usage: .section <section_name>");
          // checking if the type of operand is a string
          if(auto* section_name = std::get_if<std::string>(&($3.back()))) {
            assembler.openSection(*section_name);
          }
          else 
            error("section operand <section_name> must be a string!");
        }
        else if ($2 == "global") {
          // checking if all elements in the vector are strings
          bool all_symbols = std::all_of(
            $3.begin(), 
            $3.end(), 
            [](const Operand& op) {
              return std::holds_alternative<std::string>(op);
            }
          );

          if(!all_symbols) {
            error("global directive requires all of its operands to be strings!");
          }

          for(const Operand &symbol : $3) {
            assembler.declareGlobal(std::get<std::string>(symbol));
          }
        }
        else if ($2 == "extern") {
          // checking if all elements in the vector are strings
          bool all_symbols = std::all_of(
            $3.begin(), 
            $3.end(), 
            [](const Operand& op) {
              return std::holds_alternative<std::string>(op);
            }
          );

          if(!all_symbols) {
            error("extern directive requires all of its operands to be strings!");
          }

          for(const Operand &symbol : $3) {
            assembler.declareExtern(std::get<std::string>(symbol));
          }
        }
        else if ($2 == "word") {
          for(const Operand &symbol : $3) {
            if(std::holds_alternative<std::string>(symbol)) 
              assembler.handleWordSymbol(std::get<std::string>(symbol));
            else
              assembler.handleWordLiteral(std::get<std::uint32_t>(symbol));
          }
        }
        else if ($2 == "skip") {
          if($3.size() != 1)
            error("skip directive usage: .skip <literal>");

          // checking if the type of operand is a number
          if(auto* literal = std::get_if<std::uint32_t>(&($3.back()))) {
            // TODO
            assembler.emitZeros(*literal);
          }
          else 
            error("skip operand <literal> must be a number!");
        }
        else if ($2 == "equ") {
          // LEVEL C, do last, probably will have its own rule, like .ascii
          cout << ".equ is level C, will finish later" << endl;
        }
        else {
          error("unknown directive ." + $2);
        }
      }
  | DOT IDENT STRING 
      { 
        if ($2 == "ascii") {
          // TODO
        }
        else 
          error("unknown directive ." + $2);
      }
  ;
/* used for directives */
operand_list
  : IDENT
      {
        /* first argument */
        $$ = OperandList{ $1 };
      }
  | NUMBER 
      {
        /* first argument */
        $$ = OperandList{ $1 };
      }
  | operand_list COMMA IDENT 
      {
        /* other arguments */
        $$ = std::move($1);
        $$.push_back($3);

      }
  | operand_list COMMA NUMBER
      {
        /* other arguments */
        $$ = std::move($1);
        $$.push_back($3);
      }
  ;

label
  : IDENT COLON
      {
        assembler.defineLabel($1);
      }
  ;
/* instruction -> IDENT (halt) */
instruction
  : IDENT
      {
        if ($1 == "halt")
          cout << "instruction halt\n";
        else
          error("unknown instruction " + $1);
      }
  ;

%%

void yy::parser::error(const std::string &msg) {
  cerr << "parse error: " << msg << "\n";
  exit(1);
}
