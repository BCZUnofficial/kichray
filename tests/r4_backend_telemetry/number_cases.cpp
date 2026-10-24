};
#undef snprintf
int main(){
    Device d;
    d.Number("TEST","id","value",UINT64_MAX);
    assert(!formatted && !emitted);
    d.telemetry_sink=Sink;
    d.Number("TEST","id","value",UINT64_MAX);
    assert(!formatted && !emitted);
    d.telemetry.flags=KICH_R4_TELEMETRY_DETAILED;d.telemetry_sink=NULL;
    d.Number("TEST","id","value",UINT64_MAX);
    assert(!formatted && !emitted);
    d.telemetry_sink=Sink;
    d.Number("TEST","id","value",UINT64_MAX);
    assert(formatted==5 && emitted==1);
    assert(received.struct_size==sizeof(received));
    assert(!strcmp(received.value,"18446744073709551615"));
    assert(!strcmp(received.category,"TEST") && !strcmp(received.id,"id") && !strcmp(received.key,"value"));
    d.telemetry_sink=ThrowingSink;
    d.Number("TEST","id","value",UINT64_C(9223372036854775809));
    assert(d.callback_failures==1);
    puts("PASS: actual Number/Emit bodies: zero formatting without detailed sink, exact uint64, bounded callback failures");
}
