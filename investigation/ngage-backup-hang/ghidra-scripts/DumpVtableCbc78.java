import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpVtableCbc78 extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/vtable_cbc78_dump.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        Memory mem = currentProgram.getMemory();
        Address wordAddr = toAddr(0x804cbc78L);
        long vtableAddrVal = mem.getInt(wordAddr) & 0xFFFFFFFFL;
        out.printf("word at 0x804cbc78 = 0x%x%n", vtableAddrVal);
        Address vtableAddr = toAddr(vtableAddrVal);
        for (int i = 0; i < 8; i++) {
            Address slot = vtableAddr.add(i * 4);
            long fnAddr = mem.getInt(slot) & 0xFFFFFFFFL;
            out.printf("  slot[%d] @ 0x%s = 0x%x%n", i, slot, fnAddr);
        }
        out.close();
        println("WROTE " + outPath);
    }
}
