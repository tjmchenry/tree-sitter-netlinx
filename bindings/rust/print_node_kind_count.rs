fn main() {
    let language: tree_sitter::Language = tree_sitter_netlinx::LANGUAGE.into();
    println!("{}", language.node_kind_count());
}
