%{
  /* Lexer feeding the bison parser. parser.tab.h holds the token codes and
     the yylval union; bison generates it from parser.y -- not us. */
  #include "parser.tab.h"
%}
%option noyywrap
%option nounput noinput
%%

#.*                      ; /* skip comments */
[ \t\r]+                 ; /* skip inline whitespace           */
\n                       { return EOL; }
\.                       { return DOT; }
[a-zA-Z_][a-zA-Z0-9_]*   { yylval.sval = strdup(yytext); return IDENT; }
.                        ; /* ignore unknown chars for now     */

%%
