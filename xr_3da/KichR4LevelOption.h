#ifndef KICH_R4_LEVEL_OPTION_H
#define KICH_R4_LEVEL_OPTION_H
#include <stddef.h>
#include <string.h>
inline int KichR4_NextToken(const char*& line,char* token,size_t capacity){
    if(!line)return 0;
    while(*line==' ' || *line=='\t')++line;
    if(!*line)return 0;
    size_t n=0;bool quote=false,overflow=false;
    while(*line && (quote || (*line!=' ' && *line!='\t'))){const char c=*line++;
        if(c=='"')quote=!quote;else{if(n+1<capacity)token[n++]=c;else overflow=true;}}
    token[n]=0;return quote?-1:overflow?-2:1;
}
/* 0 absent, 1 valid, -1 malformed. A bare key selects the Cordon. */
inline int KichR4_LevelOption(const char* line,char (&level)[64]){
    char token[512];int read=0;
    while((read=KichR4_NextToken(line,token,sizeof(token)))!=0){
        if(read<0)continue;
        if(strcmp(token,"-kich_r4_level"))continue;
        read=KichR4_NextToken(line,token,sizeof(token));if(read<0)return -1;
        if(!read || token[0]=='-')memcpy(token,"l01_escape",11);
        const size_t n=strlen(token);if(!n || n>=sizeof(level))return -1;
        for(size_t i=0;i<n;++i){const char c=token[i];if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-'))return -1;}
        memcpy(level,token,n+1);return 1;
    }return 0;
}
#endif
