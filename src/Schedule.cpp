/*! ****************************************************************************
\file Schedule.cpp
\authors Ian J. Bertolacci

\brief
Generate a simple sequential, non-parallel schedule from a LoopChain.

\copyright
Copyright 2015-2016 Colorado State University
*******************************************************************************/

#include <LoopChainIR/Schedule.hpp>
#include <LoopChainIR/all_isl.hpp>
#include <LoopChainIR/util.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <unistd.h>

using namespace LoopChainIR;
using namespace std;

Schedule::Schedule( LoopChain& chain, std::string statement_prefix, std::string iterator_prefix ) :
  chain(chain), statement_prefix(statement_prefix),
  root_statement_symbol( SSTR(statement_prefix << "statement_" ) ),
  iterator_prefix( iterator_prefix ),
  manager( new Subspace("loop", 0), new Subspace("i", chain.maxDimension() )),
  depth(0)
  {

  // Synthesize the loop statements and the primary maps
  // primary map(s) needs to occur all at once, across all nests
  this->manager.get_safe_prefix( "loop" );
  this->manager.get_safe_prefix( "i" );

  ostringstream map_string;
  map_string << "{\n";

  Subspace* nest_ss = this->manager.get_nest();
  Subspace* loop_ss = this->manager.get_loops();
  nest_ss->set_aliased();
  int chain_idx = 0;
  for( LoopNest nest : this->chain ){
    RectangularDomain& domain = nest.getDomain();

    ostringstream statement_string;

    // synth. the symbolic constants
    statement_string << "[";
    bool is_not_first_symbolic = false; // for comma insertion
    for( auto symbol : domain.getSymbols() ){
      statement_string << (is_not_first_symbolic?",":"") << symbol;
      is_not_first_symbolic = true;
    }

    statement_string << "]->{" << root_statement_symbol << chain_idx << "[";
    // add statement name into map_string;
    map_string << "\t" << root_statement_symbol << chain_idx << "[";

    // build the iterators for the statement (and the map)
    for( RectangularDomain::size_type dimension = 0; dimension < domain.dimensions(); dimension += 1 ){
      statement_string << ((dimension > 0)?",":"") << "i_" << dimension;
      map_string << ((dimension > 0)?",":"") << "i_" << dimension;
    }

    statement_string << "] :";

    // Create maping tuple
    map_string << "] -> [" << this->manager.get_output_iterators() << "] : \n\t\t"
    // Map the loop constant iterator to the chain index,
    // and the nest constant to 0 i_0 .. i_n will be maped later
               << ((*loop_ss)[loop_ss->const_index]) << " = " <<  chain_idx
               << " and " << ((*nest_ss)[nest_ss->const_index]) << " = 0";

    // build the conditions for the statement and map
    for( RectangularDomain::size_type dimension = 0; dimension < domain.dimensions(); dimension += 1 ){
      // statement conditions (loop bounds)
      statement_string << ((dimension > 0)?" and ":"")
                       << domain.getLowerBound( dimension ) << " <= "
                       << "i_" << dimension
                       << " <= " << domain.getUpperBound( dimension )
                       << " and " << domain.getLowerBound( dimension )
                       << " <= " << domain.getUpperBound( dimension );
      // map conditions
      map_string << " and i_" << dimension << " = " << (*nest_ss)[dimension];
    }

    // map higher dimensions to 0
    for( RectangularDomain::size_type dimension = domain.dimensions(); dimension < nest_ss->size(); dimension += 1 ){
      map_string << " and " << (*nest_ss)[dimension] << " = 0";
    }

    // end of statement definition
    statement_string << "} ;";

    // end of this domains map
    map_string << ";\n";

    this->domains.push_back( statement_string.str() );
    chain_idx += 1;
  }

  map_string << "} ;";
  transformations.push_back( map_string.str() );

  nest_ss->unset_aliased();
  loop_ss->unset_aliased();

}

void Schedule::apply( Transformation& scheduler ){
  for( std::string transformation: scheduler.apply(*this) ){
    this->append( transformation );
  }
  this->manager.next_stage();
}

void Schedule::apply( std::vector<Transformation*> schedulers ){
  for( std::vector<Transformation*>::iterator it = schedulers.begin(); it != schedulers.end(); ++it ){
    this->apply( **it );
  }
}

Schedule::size_type Schedule::append( std::string text ){
  if( text != "" ){
    this->transformations.push_back( text );
  }

  return this->transformations.size()-1;
}

Schedule::iterator Schedule::begin_domains(){
  return this->domains.begin();
}

Schedule::iterator Schedule::end_domains(){
  return this->domains.end();
}

Schedule::iterator Schedule::begin_transformations(){
  return this->transformations.begin();
}

Schedule::iterator Schedule::end_transformations(){
  return this->transformations.end();
}


Schedule::const_iterator Schedule::begin_domains() const {
  return this->domains.begin();
}

Schedule::const_iterator Schedule::end_domains() const {
  return this->domains.end();
}

Schedule::const_iterator Schedule::begin_transformations() const {
  return this->transformations.begin();
}

Schedule::const_iterator Schedule::end_transformations() const {
  return this->transformations.end();
}


ISLASTRoot* Schedule::codegenToIslAst(){
  isl_ctx* ctx = isl_ctx_alloc();

  // Union domains together
  isl_union_set* full_domain = NULL;

  for( Schedule::iterator it = this->begin_domains(); it != this->end_domains(); ++it ){
    isl_union_set* domain = isl_union_set_read_from_str(ctx, (*it).c_str());
    if( full_domain == NULL ){
      full_domain = domain;
    } else {
      full_domain = isl_union_set_union( full_domain, domain );
    }
  }

  // Compose tranformation maps together
  isl_union_map* transformation = NULL;

  for( Schedule::iterator it = this->begin_transformations(); it != this->end_transformations(); ++it){
    isl_union_map* map = isl_union_map_read_from_str(ctx, (*it).c_str());
    transformation = (transformation)? isl_union_map_apply_range(transformation, map) : map;
  }

  // Apply transformation to schedule
  isl_union_map* schedule_map = isl_union_map_intersect_domain(transformation, full_domain);
  //isl_union_set* schedule_set = isl_union_map_domain( schedule_map );
  //isl_schedule* schedule = isl_schedule_from_domain( schedule_set );

  // Create separation option map
  SubspaceManager& manager = this->getSubspaceManager();
  Subspace* nest = manager.get_nest();

  isl_union_map* separate_map = isl_union_map_read_from_str(
    ctx,
    SSTR( "{ [" << manager.get_input_iterators() << "] -> "
        << "separate[" << nest->get( nest->size() , false ) << "] };"
    ).c_str()
  );

  // Create AST
  isl_ast_build* build = isl_ast_build_alloc(ctx);
  build = isl_ast_build_set_options( build, separate_map );

  // Create hundreds of iterator names
  {
    int dims = 100;
    isl_id_list* names = isl_id_list_alloc( ctx, dims );
    for( int d = 0; d < dims; ++d ){
      isl_id* id = isl_id_alloc( ctx, (this->getIteratorPrefix() + to_string(d)).c_str(), NULL );
      names = isl_id_list_add( names, id );
    }
    build = isl_ast_build_set_iterators( build, names );
  }

  // Collect depths of parallel loops
  std::set<Subspace::size_type> parallel_depths;
  {
    Subspace::size_type depth = 1;
    for(
      SubspaceManager::iterator cursor = manager.begin();
      cursor != manager.end();
      depth += (*cursor)->size(), ++cursor
     ){
      if( this->parallel_subspaces.count( *cursor ) != 0 ){
        parallel_depths.insert( depth + this->parallel_subspaces[*cursor] );
      }
    }
  }

  // Annotate loops of appropriate depth
  //annotateParallelISLLoops( isl_root, parallel_depths );
  isl_ast_build_set_after_each_for( build, custom_for_builder_callback, (void*) &parallel_depths );

  isl_ast_node* tree = isl_ast_build_node_from_schedule_map( build, schedule_map );

  // free ISL objects
  isl_ast_build_free( build );

  // Create the ISL AST Root object
  ISLASTRoot* isl_root = new ISLASTRoot( tree, ctx );

  return isl_root;
}

std::string Schedule::codegen( ){
  // Get ISL AST Tree
  ISLASTRoot& root = *this->codegenToIslAst();
  isl_ctx* ctx = root.ctx;
  isl_ast_node* tree = root.root;

  // Write code to string
  isl_printer* p = isl_printer_to_str(ctx);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  isl_ast_print_options* print_options = isl_ast_print_options_alloc(ctx);
  // Set option to print for nodes with my printer (custom_for_printer_callback)
  print_options = isl_ast_print_options_set_print_for(print_options, custom_for_printer_callback, NULL);
  isl_ast_node_print(tree, p, print_options);

  // Extract string
  string code_text( isl_printer_get_str( p ) );

  isl_printer_free( p );
  isl_ast_node_free( tree );
  // TODO Why do objects still ref this?
  // TODO Seems to be symptom of the custom_for_builder_callback.
  // This is disabled to prevent scary warnings
  //isl_ctx_free( ctx );

  return code_text;
}

bool Schedule::codegenToFile( std::string file_name ){
  std::string code = this->codegen();
  std::ofstream file_stream( file_name );

  if( file_stream ){
    file_stream << code;
    file_stream.flush();
  }

  return file_stream.good() && !( file_stream.fail() || file_stream.bad() );
}

std::string Schedule::codegenToISCC( ) const {
  std::ostringstream os;
  os << "# Domains:" << std::endl;
  int stmt_count = 1;
  for( Schedule::const_iterator it = this->begin_domains(); it != this->end_domains(); ++it ){
    os << "S" << stmt_count++ << " := " << (*it) << std::endl;
  }

  os << std::endl << "# Transformations:" << std::endl;
  int map_count = 1;
  for( Schedule::const_iterator it = this->begin_transformations(); it != this->end_transformations(); ++it ){
    os << "M" << map_count++ << " := " <<  (*it) << std::endl;
  }

  os << "\ncodegen( (";
  for( int i = 1; i < map_count; i += 1 ){
    os << ((i>1)?".":"") << "M" << i;
  }
  os << ")*(";
  for( int i = 1; i < stmt_count; i += 1 ){
    os << ((i>1)?"+":"") << "S" << i;
  }
  const Subspace* nest = manager.get_nest();;
  os << ") ) using { [" << manager.get_input_iterators() << "] -> "
      << "separate[" << nest->get( nest->size() , false ) << "] };";
  return std::string( os.str() );
}

RectangularDomain::size_type Schedule::getIteratorsLength() {
  return (RectangularDomain::size_type) this->manager.size(); //this->iterators_length;
}

LoopChain& Schedule::getChain(){
  return this->chain;
}

std::string Schedule::getStatementPrefix(){
  return std::string(this->statement_prefix);
}

std::string Schedule::getRootStatementSymbol(){
  return std::string(this->root_statement_symbol);
}

std::string Schedule::getIteratorPrefix(){
  return std::string( this->iterator_prefix );
}

SubspaceManager& Schedule::getSubspaceManager(){
  return this->manager;
}

int Schedule::getDepth(){
  return this->depth;
}

int Schedule::incrementDepth(){
  this->depth += 1;
  return this->getDepth();
}

int Schedule::decrementDepth(){
  this->depth -= 1;
  return this->getDepth();
}

void Schedule::addParallelSubspace( Subspace* subspace, Subspace::size_type additional_depth ){
  this->parallel_subspaces[subspace] = additional_depth;
}

std::ostream& LoopChainIR::operator<<( std::ostream& os, const Schedule& schedule){
  return os << schedule.codegenToISCC() ;
}

__isl_give isl_ast_node* LoopChainIR::custom_for_builder_callback( __isl_take isl_ast_node *node, __isl_keep isl_ast_build* build, void* user ){
  // Get dimensionality of loop nest at this point.
  unsigned dimensions = isl_space_dim( isl_ast_build_get_schedule_space( build ), isl_dim_set );
  // Magic cast void* to std::set<Subspace::size_type>*
  std::set<Subspace::size_type>* depths = static_cast<std::set<Subspace::size_type>*>( user );

  // If no the appropriate depth, return exiting, unmodified node
  if( depths->count(dimensions) == 0 ){
    return node;
  }

  // Create annotation
  string annotation_str = "parallel annotation";
  isl_id* annotation = isl_id_alloc( isl_ast_build_get_ctx(build), annotation_str.c_str(), NULL );
  assertWithException( annotation != NULL, "Failed to create annotation in custom_for_builder_callback." );

  // Add annotation
  isl_ast_node* new_node = isl_ast_node_set_annotation( node, annotation );
  assertWithException( new_node != NULL, "Failed to create annotated node during custom_for_builder_callback." );

  return new_node;
}

__isl_give isl_printer* LoopChainIR::custom_for_printer_callback( __isl_take isl_printer *p, __isl_take isl_ast_print_options *options, __isl_keep isl_ast_node *node, void *user __attribute__((unused)) ){
  // Get annotation
  isl_id* maybe_annotation = isl_ast_node_get_annotation( node );
  // If annotation is not null, and if string is the parallel annotation string print openmp annotation
  if( maybe_annotation != NULL && string( isl_id_get_name( maybe_annotation ) ) == string("parallel annotation") ){
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "#pragma omp parallel for");
    p = isl_printer_end_line(p);
  }

  // print the for node as usual
  p = isl_ast_node_for_print(node, p, options);
  return p;
}
