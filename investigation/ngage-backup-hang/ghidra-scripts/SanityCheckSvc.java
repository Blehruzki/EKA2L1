import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

import java.io.FileWriter;
import java.io.PrintWriter;

public class SanityCheckSvc extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/sanity_svc.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        // We KNOW FUN_803a02cc = software_interrupt(0x4c). Print its raw bytes.
        Address a = toAddr(0x803a02ccL);
        for (int i = 0; i < 4; i++) {
            byte b = getByte(a.add(i));
            out.printf("byte[%d] @ 0x%s = 0x%02x%n", i, a.add(i), b & 0xFF);
        }
        out.close();
        println("WROTE " + outPath);
    }
}
