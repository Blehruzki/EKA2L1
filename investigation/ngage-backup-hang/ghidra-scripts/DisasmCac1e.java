import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DisasmCac1e extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/disasm_cac1e.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        Address a = toAddr(0x804cac1eL);
        Instruction instr = getInstructionAt(a);
        if (instr == null) { try { disassemble(a); instr = getInstructionAt(a); } catch (Exception e) {} }
        for (int i = 0; i < 40 && instr != null; i++) {
            out.printf("%s: %s%n", instr.getAddress(), instr.toString());
            instr = instr.getNext();
        }
        out.close();
        println("WROTE " + outPath);
    }
}
