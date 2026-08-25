import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

import java.io.FileWriter;
import java.io.PrintWriter;

// Round 53: 0x83486a50 holds a data reference to 0x83485fdf (the vtable slot for
// 0x83485fde). Dump the surrounding bytes as a table of 4-byte words to see the
// vtable layout - other slots might be named/recognizable, or at least show
// whether this is a small (few-method) or large (many-method) interface.
public class DumpVtable83486a50 extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/vtable_83486a50.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Address base = toAddr(0x83486a30L);
        for (int i = 0; i < 24; i++) {
            Address a = base.add((long) i * 4);
            try {
                int val = currentProgram.getMemory().getInt(a);
                out.printf("%s: 0x%08x%n", a, val);
            } catch (Exception e) {
                out.printf("%s: <read failed>%n", a);
            }
        }

        out.close();
        println("WROTE " + outPath);
    }
}
