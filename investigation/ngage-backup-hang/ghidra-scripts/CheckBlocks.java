import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.listing.FunctionIterator;

import java.io.FileWriter;
import java.io.PrintWriter;

public class CheckBlocks extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/ghidra_checkblocks.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            out.println("block " + block.getName() + " [" + block.getStart() + " - " + block.getEnd() + "] initialized=" + block.isInitialized());
            int insnCount = 0;
            InstructionIterator ins = currentProgram.getListing().getInstructions(block.getStart(), true);
            while (ins.hasNext()) {
                Instruction i = ins.next();
                if (i.getAddress().compareTo(block.getEnd()) > 0) break;
                insnCount++;
            }
            int funcCount = 0;
            FunctionIterator fit = currentProgram.getFunctionManager().getFunctions(block.getStart(), true);
            while (fit.hasNext()) {
                if (fit.next().getEntryPoint().compareTo(block.getEnd()) > 0) break;
                funcCount++;
            }
            out.println("  instructions in block: " + insnCount + "   functions starting in block: " + funcCount);

            // sample first 10 bytes as hex + first few instructions
            try {
                byte[] sample = new byte[16];
                block.getBytes(block.getStart(), sample);
                StringBuilder sb = new StringBuilder();
                for (byte b : sample) sb.append(String.format("%02x ", b));
                out.println("  first bytes: " + sb);
            } catch (Exception e) {
                out.println("  couldn't read bytes: " + e.getMessage());
            }
        }
        out.close();
        println("WROTE " + outPath);
    }
}
