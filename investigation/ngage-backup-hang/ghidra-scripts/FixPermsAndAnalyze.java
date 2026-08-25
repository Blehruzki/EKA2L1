import ghidra.app.script.GhidraScript;
import ghidra.program.model.mem.MemoryBlock;

public class FixPermsAndAnalyze extends GhidraScript {
    @Override
    protected void run() throws Exception {
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            if (block.getName().equals("ecom.dll") || block.getName().equals("ecomserver.exe")) {
                block.setRead(true);
                block.setWrite(false);
                block.setExecute(true);
                println("fixed perms on " + block.getName() + " execute=" + block.isExecute());
            }
        }
        println("running analyzeAll...");
        analyzeAll(currentProgram);
        println("done");
    }
}
