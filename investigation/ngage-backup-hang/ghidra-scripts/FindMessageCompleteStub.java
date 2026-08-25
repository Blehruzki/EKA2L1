import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Function;

import java.io.FileWriter;
import java.io.PrintWriter;

public class FindMessageCompleteStub extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/message_complete_stub.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        // Scan a range of euser.dll for "svc 0x42" / "svc 0x43" pattern (software_interrupt calls)
        // Thumb SWI encoding: 0xDF <imm8>, so bytes are DF 42 or DF 43 for these specific values? Actually
        // Thumb SWI is 1101 1111 imm8 = 0xDF imm8. So bytes: DF 42 (svc 0x42), DF 43 (svc 0x43).
        Address start = toAddr(0x80399000L);
        Address end = toAddr(0x803c0000L);
        Address a = start;
        int found = 0;
        while (a.compareTo(end) < 0 && found < 20) {
            try {
                byte b0 = getByte(a);
                byte b1 = getByte(a.add(1));
                if ((b1 & 0xFF) == 0xDF && ((b0 & 0xFF) == 0x42 || (b0 & 0xFF) == 0x43)) {
                    Function f = getFunctionContaining(a);
                    out.printf("SVC 0x%x at 0x%s in %s%n", b0 & 0xFF, a, f != null ? f.getName() + " @ " + f.getEntryPoint() : "(none)");
                    found++;
                }
            } catch (Exception e) {}
            a = a.add(2);
        }
        out.close();
        println("WROTE " + outPath);
    }
}
