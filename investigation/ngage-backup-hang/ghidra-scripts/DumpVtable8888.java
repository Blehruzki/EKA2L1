import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpVtable8888 extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/vtable_8888_dump.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        Memory mem = currentProgram.getMemory();
        Address wordAddr = toAddr(0x804c8888L);
        long vtableAddrVal = mem.getInt(wordAddr) & 0xFFFFFFFFL;
        out.printf("word at 0x804c8888 = 0x%x (this should be the vtable address)%n", vtableAddrVal);
        Address vtableAddr = toAddr(vtableAddrVal);
        for (int i = 0; i < 12; i++) {
            Address slot = vtableAddr.add(i * 4);
            try {
                long fnAddr = mem.getInt(slot) & 0xFFFFFFFFL;
                Function f = getFunctionAt(toAddr(fnAddr));
                out.printf("  slot[%d] @ 0x%s = 0x%x -> %s%n", i, slot, fnAddr, f != null ? f.getName() : "(no function)");
            } catch (Exception e) {
                out.printf("  slot[%d] @ 0x%s -> (error: %s)%n", i, slot, e.getMessage());
            }
        }
        out.close();
        println("WROTE " + outPath);
    }
}
