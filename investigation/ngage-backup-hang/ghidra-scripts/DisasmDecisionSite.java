import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Function;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DisasmDecisionSite extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/disasm_700022a4.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Address target = toAddr(0x700022A4L);
        Function f = getFunctionContaining(target);
        out.printf("0x700022A4 is in function: %s%n%n", f != null ? (f.getName() + "@" + f.getEntryPoint()) : "UNKNOWN");

        Address start = (f != null) ? f.getEntryPoint() : toAddr(0x70002200L);
        Instruction instr = getInstructionAt(start);
        if (instr == null) {
            disassemble(start);
            instr = getInstructionAt(start);
        }
        for (int i = 0; i < 80 && instr != null; i++) {
            byte[] bytes = instr.getBytes();
            StringBuilder hex = new StringBuilder();
            for (byte b : bytes) hex.append(String.format("%02x ", b));
            String marker = instr.getAddress().equals(target) ? "  <=== STACK VALUE POINTS HERE" : "";
            out.printf("%s: %s   [%s]%s%n", instr.getAddress(), instr.toString(), hex.toString().trim(), marker);
            instr = instr.getNext();
        }
        out.close();
        println("WROTE " + outPath);
    }
}
