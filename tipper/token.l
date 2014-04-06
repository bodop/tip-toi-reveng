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

"attempts" { return ATTEMPTS; }
"bonus" { return BONUS; }
"bonusgame" { return BONUSGAME; }
"bye" { return BYE; }
"entry_score" { return ENTRY_SCORE; }
"false" { return FALSE; }
"format" { return FORMAT; }
"game" { return GAME; }
"next_bonus_round" { return NEXT_BONUS_ROUND; }
"last_bonus_round" { return LAST_BONUS_ROUND; }
"invalid" { return INVALID; }
"last_round" { return LAST_ROUND; }
"next_level" { return NEXT_LEVEL; }
"next_round" { return NEXT_ROUND; }
"product" { return PRODUCT; } 
"publication" { return PUBLICATION; } 
"raw_xor" { return RAW_XOR; } 
"magic_xor" { return MAGIC_XOR; } 
"oids" { return OIDS; }
"ok" { return OK; }
"play" { return PLAY; }
"pre_last_round_count" { return PRE_LAST_ROUND_COUNT; }
"register" { return REGISTER; }
"repeat_oid" { return REPEAT_OID; }
"rounds" { return ROUNDS; } 
"score" { return SCORE; }
"subgame" { return SUBGAME; }
"type" { return TYPE; }
"language" { return LANGUAGE; }
"unknown" { return UNKNOWN; }
"welcome" {  return WELCOME; }
"u0" { return U0; }
"u1" { return U1; }
"u3" { return U3; }
"u4" { return U4; }
"u2" { return U2; }
"u6" { return U6; }
"u7" { return U7; }
"u8" { return U8; }
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
