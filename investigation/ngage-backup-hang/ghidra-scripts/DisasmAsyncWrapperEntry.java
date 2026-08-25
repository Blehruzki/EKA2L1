import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DisasmAsyncWrapperEntry extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/async_wrapper_entry_disasm.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Address a = toAddr(0x803aa976L);
        Instruction instr = getInstructionAt(a);
        for (int i = 0; i < 6 && instr != null; i++) {
            byte[] bytes = instr.getBytes();
            StringBuilder hex = new StringBuilder();
            for (byte b : bytes) hex.append(String.format("%02x ", b));
            out.printf("%s: %s   [%s]%n", instr.getAddress(), instr.toString(), hex.toString().trim());
            instr = instr.getNext();
        }
        out.close();
        println("WROTE " + outPath);
    }
}
