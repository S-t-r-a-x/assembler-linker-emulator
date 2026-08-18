%{
  /* Lexer feeding the bison parser. parser.tab.h holds the token codes and
     the yylval variant type; bison generates it from parser.y -- not us. */
  #include "parser.tab.h"
  #define YY_DECL int yylex(yy::parser::semantic_type *yylval)
%}
%option noyywrap
%option nounput noinput
%%
  /* Synthetic EOL so a file with no newline at its end still finishes its statement
     Making EOL optional in the grammar (line: statement) instead costs 2 shift/reduce conflicts. */
<<EOF>>                     { 
                              static bool sent = false;
                              if (!sent) { sent = true; return yy::parser::token::EOL; }
                              yyterminate(); 
                            } 
#.*                         ; /* skip comments */
[ \t\r]+                    ; /* skip inline whitespace */
\n                          { return yy::parser::token::EOL; }
\.                          { return yy::parser::token::DOT; }
[a-zA-Z_][a-zA-Z0-9_]*      { 
                              yylval->emplace<std::string>(yytext); 
                              return yy::parser::token::IDENT; 
                            }
(0[xX][0-9a-fA-F]+|[0-9]+)  { 
                              yylval->emplace<std::uint32_t>(strtoul(yytext, nullptr, 0)); 
                              return yy::parser::token::NUMBER; 
                            }
\"([^\"\\]|\\.)*\"          { 
                              yylval->emplace<std::string>(yytext + 1, yyleng - 2); 
                              return yy::parser::token::STRING; 
                            }

%r(1[0-5]|[0-9])            {
                              yylval->emplace<std::uint32_t>(strtoul(yytext + 2, nullptr, 10));
                              return yy::parser::token::REGISTER;
                            }
%sp                         { 
                              yylval->emplace<std::uint32_t>(14); 
                              return yy::parser::token::REGISTER; 
                            }
%pc                         { 
                              yylval->emplace<std::uint32_t>(15); 
                              return yy::parser::token::REGISTER; 
                            }

%status                     { 
                              yylval->emplace<std::uint32_t>(0); 
                              return yy::parser::token::CSR; 
                            }
%handler                    { 
                              yylval->emplace<std::uint32_t>(1); 
                              return yy::parser::token::CSR; 
                            }
%cause                      { 
                              yylval->emplace<std::uint32_t>(2); 
                              return yy::parser::token::CSR; 
                            }

%[a-zA-Z_][a-zA-Z0-9_]*     { return yy::parser::token::UNKNOWN; }

\$                          { return yy::parser::token::DOLLAR; }
\[                          { return yy::parser::token::LBRACKET; }
\]                          { return yy::parser::token::RBRACKET; }
\+                          { return yy::parser::token::PLUS; }
,                           { return yy::parser::token::COMMA; }
:                           { return yy::parser::token::COLON; }
.                           ; /* ignore unknown chars for now */

%%
