import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DisasmCbc31 extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/disasm_cbc31.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        // dump a window around the target: 0x40 bytes before, 0x40 after
        Address start = toAddr(0x804cbbf0L);
        Address a = start;
        for (int i = 0; i < 60; i++) {
            Instruction instr = getInstructionAt(a);
            if (instr == null) {
                try { disassemble(a); instr = getInstructionAt(a); } catch (Exception e) {}
            }
            if (instr != null) {
                out.printf("%s: %s%n", instr.getAddress(), instr.toString());
                a = instr.getMinAddress().add(instr.getLength());
            } else {
                out.printf("%s: (no instr)%n", a);
                a = a.add(2);
            }
        }
        out.close();
        println("WROTE " + outPath);
    }
}
