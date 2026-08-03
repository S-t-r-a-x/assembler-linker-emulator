/* Content here lands in parser.tab.h too, not just parser.tab.c -- needed
   because %parse-param puts Assembler& into yyparse()'s declaration in
   the header, and lexer.l includes only the header, not this file. */

%code requires {
  #include "../../inc/assembler.hpp"
  void yyerror(Assembler &assembler, const char* s);
}

%{
  #include <cstdio>
  #include <cstring>
  #include <iostream>
  #include <string>
  using namespace std;

  extern int yylex();
  extern FILE* yyin;

%}

/* Value carried by tokens/nonterminals. Only IDENT needs one for now. */
%union {
  char* sval;
}
%parse-param { Assembler &assembler }
%token DOT EOL
%token <sval> IDENT

%%

/* program -> line*                                                        */
program
  : /* empty */
  | program line
  ;

/* line -> EOL | statement EOL | statement */
line
  : EOL
  | statement EOL
  | statement
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
  : DOT IDENT IDENT
      {
        if (strcmp($2, "section") == 0)
          cout << "directive .section " << $3 << "\n";
        else
          yyerror(assembler, ("unknown directive ." + string($2)).c_str());
        free($2); free($3);
      }
  | DOT IDENT
      {
        if (strcmp($2, "end") == 0)
          cout << "directive .end\n";
        else
          yyerror(assembler, ("unknown directive ." + string($2)).c_str());
        free($2);
      }
  ;

/* instruction -> IDENT   (halt)                                           */
instruction
  : IDENT
      {
        if (strcmp($1, "halt") == 0)
          cout << "instruction halt\n";
        else
          yyerror(assembler ,("unknown instruction " + string($1)).c_str());
        free($1);
      }
  ;

%%

int main(int argc, char** argv) {
  if (argc > 1) {
    yyin = fopen(argv[1], "r");
    if (!yyin) { cerr << "cannot open " << argv[1] << "\n"; return 1; }
  }
  Assembler as;
  return yyparse(as);  /* reads yyin (defaults to stdin if no file given) */
}

void yyerror(Assembler &assembler, const char* s) {
  cerr << "parse error: " << s << "\n";
  exit(1);
}
