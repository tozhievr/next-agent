#pragma once
namespace lx::host {
// Resolve names and ranges from declaration nodes. Parser wrappers/identifier tokens
// are not declaration ranges, and function parameter/type names are not function names.
inline std::vector<DocumentSymbolNode> declaration_outline(const Snapshot& snapshot) {
 using Node=lx::uast::GreenNode;struct At{const Node* node=nullptr;uint32_t off=0;};auto mod=snapshot.module.get();
 auto normalized=[](std::string_view input){std::string s;for(unsigned char c:input)if(c!='_')s+=char(std::tolower(c));return s;};
 auto kind=[&](const Node* n){return n&&!n->is_seq()?normalized(mod->symbol_name(lx::uast::kind_symbol(n->kind))):std::string{};};
 auto children=[&](At p,auto fn){if(p.node&&p.node->is_inner())for(auto& c:p.node->as_inner()->children()){if(c.node){fn(At{c.node,p.off});p.off+=c.node->width;}}};
 auto symbol_kind=[](const std::string& k){
  if(k=="functiondeclaration"||k=="functiondefinition"||k=="functiondef"||k=="functiondecl"||k=="functionitem"||k=="function")return 12;
  if(k=="methoddeclaration"||k=="methoddefinition"||k=="methoddecl")return 6;
  if(k=="classdeclaration"||k=="classdefinition"||k=="classdecl"||k=="classitem")return 5;
  if(k=="structdeclaration"||k=="structdefinition"||k=="structitem")return 23;
  if(k=="interfacedeclaration")return 11;if(k=="enumdeclaration"||k=="enumitem")return 10;
  if(k=="namespacedeclaration"||k=="moduledeclaration")return 3;
  if(k=="variabledeclaration"||k=="propertydeclaration"||k=="letstatement")return 13;
  return 0;
 };
 std::function<At(At)> find_name;
 find_name=[&](At p)->At{
  if(!p.node)return {};auto k=kind(p.node);if(p.node->is_token()){
   if(!p.node->is_hidden()&&(k=="identifier"||k=="id"||k=="name"||k=="label"||k=="simplename"||k=="simpleidentifier"))return p;return {};
  }
  if(k.find("parameter")!=std::string::npos||k.find("attribute")!=std::string::npos||k.find("annotation")!=std::string::npos||k.find("modifier")!=std::string::npos||k.find("body")!=std::string::npos||k=="block"||k.find("statement")!=std::string::npos||k.find("expression")!=std::string::npos||k=="type"||k=="returntype"||k=="typespecifier"||k=="fullyspecifiedtype"||k=="typename"||k=="typehint"||k=="typeannotation")return {};
  At result;
  // Prioritize a direct name field/node over types and generic header wrappers.
  std::function<void(At)> direct=[&](At q){children(q,[&](At c){if(result.node)return;if(c.node->is_seq()){direct(c);return;}auto name=kind(c.node);if(name=="identifier"||name=="id"||name=="name"||name=="simpleidentifier"||name=="functionname"||name=="methodname"||name=="classname"||name=="declaratorid")result=find_name(c);});};direct(p);
  if(!result.node)children(p,[&](At c){if(!result.node)result=find_name(c);});return result;
 };
 std::function<void(At,std::vector<DocumentSymbolNode>&,int)> walk=[&](At p,std::vector<DocumentSymbolNode>& out,int depth){if(!p.node||depth>512||p.node->is_token()||p.node->is_error()||p.node->is_missing())return;int type=symbol_kind(kind(p.node));if(type){auto id=find_name(p);if(id.node){DocumentSymbolNode symbol;symbol.name=snapshot.rope.slice_text(id.off,id.off+id.node->width);symbol.kind=type;symbol.range={p.off,p.off+p.node->width};symbol.selection_range={id.off,id.off+id.node->width};children(p,[&](At c){walk(c,symbol.children,depth+1);});out.push_back(std::move(symbol));return;}}children(p,[&](At c){walk(c,out,depth+(p.node->is_seq()?0:1));});};
 std::vector<DocumentSymbolNode> result;walk({snapshot.root.get(),0},result,0);return result;
}
}
