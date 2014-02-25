%{

/*
 * token.l
 */

#include <parser.h>

%}

%option noinput
%option nounput
%option noyywrap
%option yylineno

COMMENT #[^\n]*

WS  [ \t\r\n]
STRING "\""[^\"]*"\""
ID  $[_A-Za-z0-9_]+
NUM [0-9]+
HEX "0x"[0-9a-fA-F]+
%%

"format" {
  return FORMAT;
}

"product" {
  return PRODUCT;
}

"publication" {
  return PUBLICATION;
}

"raw_xor" {
  return RAW_XOR;
}

"magic_xor" {
  return MAGIC_XOR;
}

"register" {
  return REGISTER;
}

"language" {
  return LANGUAGE;
}

"welcome" {  return WELCOME; }

"+=" { return ADD; }
"&&" { return AND; }
"==" { return OPERATOR_EQ; }
">=" { return OPERATOR_GE; }
"!=" { return OPERATOR_NEQ; }

{ID} {
  yylval.name=strdup(yytext);
  return IDENTIFIER;
}

{WS}

{COMMENT}

{STRING} {
  int l=strlen(yytext)-2;
  yylval.name=malloc(l+1);
  memcpy(yylval.name,yytext+1,l);
  yylval.name[l]=0;
  return STRING;
}

{HEX} {
  yylval.u32=strtol(yytext,NULL,16);
  return NUM;
}

{NUM} {
  yylval.u32=atol(yytext);
  return NUM;
}

. {
  return yytext[0];
}

%%

int parse(FILE* f) {
    yyin=f;
    return yylex();
}
