#include "project.h"
#include "config.h"
#include "terminal.h"
#include "filesystem.h"
#include <fstream>
#ifdef JUCI_ENABLE_DEBUG
#include "debug.h"
#endif

std::unordered_map<std::string, std::string> Project::run_arguments;
std::unordered_map<std::string, std::string> Project::debug_run_arguments;
std::atomic<bool> Project::compiling(false);
std::atomic<bool> Project::debugging(false);

std::unique_ptr<CMake> ProjectClang::get_cmake() {
  boost::filesystem::path path;
  if(notebook.get_current_page()!=-1)
    path=notebook.get_current_view()->file_path.parent_path();
  else
    path=Directories::get().current_path;
  if(path.empty())
    return nullptr;
  auto cmake=std::unique_ptr<CMake>(new CMake(path));
  if(cmake->project_path.empty())
    return nullptr;
  if(!CMake::create_default_build(cmake->project_path))
    return nullptr;
  return cmake;
}

std::pair<std::string, std::string> ProjectClang::get_run_arguments() {
  auto cmake=get_cmake();
  if(!cmake)
    return {"", ""};
  
  auto project_path=cmake->project_path.string();
  auto run_arguments_it=run_arguments.find(project_path);
  std::string arguments;
  if(run_arguments_it!=run_arguments.end())
    arguments=run_arguments_it->second;
  
  if(arguments.empty()) {
    auto executable=cmake->get_executable(notebook.get_current_page()!=-1?notebook.get_current_view()->file_path:"").string();
    
    if(executable!="") {
      auto project_path=cmake->project_path;
      auto build_path=CMake::get_default_build_path(project_path);
      if(!build_path.empty()) {
        size_t pos=executable.find(project_path.string());
        if(pos!=std::string::npos)
          executable.replace(pos, project_path.string().size(), build_path.string());
      }
      arguments=filesystem::escape_argument(executable);
    }
    else
      arguments=filesystem::escape_argument(CMake::get_default_build_path(cmake->project_path));
  }
  
  return {project_path, arguments};
}

void ProjectClang::compile() {    
  auto cmake=get_cmake();
  if(!cmake)
    return;
  
  auto default_build_path=CMake::get_default_build_path(cmake->project_path);
  if(default_build_path.empty())
    return;
  compiling=true;
  Terminal::get().print("Compiling project "+cmake->project_path.string()+"\n");
  Terminal::get().async_process(Config::get().terminal.make_command, default_build_path, [this](int exit_status) {
    compiling=false;
  });
}

void ProjectClang::compile_and_run() {
  auto cmake=get_cmake();
  if(!cmake)
    return;
  auto project_path=cmake->project_path;
  
  auto default_build_path=CMake::get_default_build_path(project_path);
  if(default_build_path.empty())
    return;
  
  auto run_arguments_it=run_arguments.find(project_path.string());
  std::string arguments;
  if(run_arguments_it!=run_arguments.end())
    arguments=run_arguments_it->second;
  
  if(arguments.empty()) {
    arguments=cmake->get_executable(notebook.get_current_page()!=-1?notebook.get_current_view()->file_path:"").string();
    if(arguments.empty()) {
      Terminal::get().print("Could not find add_executable in the following paths:\n");
      for(auto &path: cmake->paths)
        Terminal::get().print("  "+path.string()+"\n");
      Terminal::get().print("Solution: either use Project Set Run Arguments, or open a source file within a directory where add_executable is set.\n", true);
      return;
    }
    size_t pos=arguments.find(project_path.string());
    if(pos!=std::string::npos)
      arguments.replace(pos, project_path.string().size(), default_build_path.string());
    arguments=filesystem::escape_argument(arguments);
  }
  
  compiling=true;
  Terminal::get().print("Compiling and running "+arguments+"\n");
  Terminal::get().async_process(Config::get().terminal.make_command, default_build_path, [this, arguments, default_build_path](int exit_status){
    compiling=false;
    if(exit_status==EXIT_SUCCESS) {
      Terminal::get().async_process(arguments, default_build_path, [this, arguments](int exit_status){
        Terminal::get().async_print(arguments+" returned: "+std::to_string(exit_status)+'\n');
      });
    }
  });
}

#ifdef JUCI_ENABLE_DEBUG
std::pair<std::string, std::string> ProjectClang::debug_get_run_arguments() {
  auto cmake=get_cmake();
  if(!cmake)
    return {"", ""};
  
  auto project_path=cmake->project_path.string();
  auto run_arguments_it=debug_run_arguments.find(project_path);
  std::string arguments;
  if(run_arguments_it!=debug_run_arguments.end())
    arguments=run_arguments_it->second;
  
  if(arguments.empty()) {
    auto executable=cmake->get_executable(notebook.get_current_page()!=-1?notebook.get_current_view()->file_path:"").string();
    
    if(executable!="") {
      auto project_path=cmake->project_path;
      auto build_path=CMake::get_debug_build_path(project_path);
      if(!build_path.empty()) {
        size_t pos=executable.find(project_path.string());
        if(pos!=std::string::npos)
          executable.replace(pos, project_path.string().size(), build_path.string());
      }
      arguments=filesystem::escape_argument(executable);
    }
    else
      arguments=filesystem::escape_argument(CMake::get_debug_build_path(cmake->project_path));
  }
  
  return {project_path, arguments};
}

void ProjectClang::debug_start(std::function<void(const std::string &status)> status_callback,
                               std::function<void(const boost::filesystem::path &file_path, int line_nr, int line_index)> stop_callback) {
  auto cmake=get_cmake();
  if(!cmake)
    return;
  auto project_path=cmake->project_path;
      
  auto debug_build_path=CMake::get_debug_build_path(project_path);
  if(debug_build_path.empty())
    return;
  if(!CMake::create_debug_build(project_path))
    return;
  
  auto run_arguments_it=debug_run_arguments.find(project_path.string());
  std::string run_arguments;
  if(run_arguments_it!=debug_run_arguments.end())
    run_arguments=run_arguments_it->second;
  
  if(run_arguments.empty()) {
    run_arguments=cmake->get_executable(notebook.get_current_page()!=-1?notebook.get_current_view()->file_path:"").string();
    if(run_arguments.empty()) {
      Terminal::get().print("Could not find add_executable in the following paths:\n");
      for(auto &path: cmake->paths)
        Terminal::get().print("  "+path.string()+"\n");
      Terminal::get().print("Solution: either use Debug Set Run Arguments, or open a source file within a directory where add_executable is set.\n", true);
      return;
    }
    size_t pos=run_arguments.find(project_path.string());
    if(pos!=std::string::npos)
      run_arguments.replace(pos, project_path.string().size(), debug_build_path.string());
    run_arguments=filesystem::escape_argument(run_arguments);
  }
  
  auto breakpoints=std::make_shared<std::vector<std::pair<boost::filesystem::path, int> > >();
  for(int c=0;c<notebook.size();c++) {
    auto view=notebook.get_view(c);
    if(project_path==view->project_path) {
      auto iter=view->get_buffer()->begin();
      if(view->get_source_buffer()->get_source_marks_at_iter(iter, "debug_breakpoint").size()>0)
        breakpoints->emplace_back(view->file_path, iter.get_line()+1);
      while(view->get_source_buffer()->forward_iter_to_source_mark(iter, "debug_breakpoint"))
        breakpoints->emplace_back(view->file_path, iter.get_line()+1);
    }
  }
  
  debugging=true;
  Terminal::get().print("Compiling and debugging "+run_arguments+"\n");
  Terminal::get().async_process(Config::get().terminal.make_command, debug_build_path, [this, breakpoints, run_arguments, debug_build_path, status_callback, stop_callback](int exit_status){
    if(exit_status!=EXIT_SUCCESS)
      debugging=false;
    else {
      debug_start_mutex.lock();
      Debug::get().start(run_arguments, debug_build_path, *breakpoints, [this, run_arguments](int exit_status){
        debugging=false;
        Terminal::get().async_print(run_arguments+" returned: "+std::to_string(exit_status)+'\n');
      }, status_callback, stop_callback);
      debug_start_mutex.unlock();
    }
  });
}

void ProjectClang::debug_continue() {
  Debug::get().continue_debug();
}

void ProjectClang::debug_stop() {
  if(debugging)
    Debug::get().stop();
}

void ProjectClang::debug_kill() {
  if(debugging)
    Debug::get().kill();
}

void ProjectClang::debug_step_over() {
  if(debugging)
    Debug::get().step_over();
}

void ProjectClang::debug_step_into() {
  if(debugging)
    Debug::get().step_into();
}

void ProjectClang::debug_step_out() {
  if(debugging)
    Debug::get().step_out();
}

void ProjectClang::debug_backtrace() {
  if(debugging && notebook.get_current_page()!=-1) {
    auto backtrace=Debug::get().get_backtrace();
    
    auto view=notebook.get_current_view();
    auto iter=view->get_iter_for_dialog();
    view->selection_dialog=std::unique_ptr<SelectionDialog>(new SelectionDialog(*view, view->get_buffer()->create_mark(iter), true, true));
    auto rows=std::make_shared<std::unordered_map<std::string, Debug::Frame> >();
    if(backtrace.size()==0)
      return;
    
    for(auto &frame: backtrace) {
      std::string row="<i>"+frame.module_filename+"</i>";
      
      //Shorten frame.function_name if it is too long
      if(frame.function_name.size()>120) {
        frame.function_name=frame.function_name.substr(0, 58)+"...."+frame.function_name.substr(frame.function_name.size()-58);
      }
      if(frame.file_path.empty())
        row+=" - "+Glib::Markup::escape_text(frame.function_name);
      else {
        auto file_path=boost::filesystem::path(frame.file_path).filename().string();
        row+=":<b>"+Glib::Markup::escape_text(file_path)+":"+std::to_string(frame.line_nr)+"</b> - "+Glib::Markup::escape_text(frame.function_name);
      }
      (*rows)[row]=frame;
      view->selection_dialog->add_row(row);
    }
    
    view->selection_dialog->on_select=[this, rows](const std::string& selected, bool hide_window) {
      auto frame=rows->at(selected);
      if(!frame.file_path.empty()) {
        notebook.open(frame.file_path);
        if(notebook.get_current_page()!=-1) {
          auto view=notebook.get_current_view();
          
          Debug::get().select_frame(frame.index);
          
          view->get_buffer()->place_cursor(view->get_buffer()->get_iter_at_line_index(frame.line_nr-1, frame.line_index-1));
          
          while(g_main_context_pending(NULL))
            g_main_context_iteration(NULL, false);
          if(notebook.get_current_page()!=-1 && notebook.get_current_view()==view)
            view->scroll_to(view->get_buffer()->get_insert(), 0.0, 1.0, 0.5);
        }
      }
    };
    view->selection_dialog->show();
  }
}

void ProjectClang::debug_show_variables() {
  if(debugging && notebook.get_current_page()!=-1) {
    auto variables=Debug::get().get_variables();
    
    auto view=notebook.get_current_view();
    auto iter=view->get_iter_for_dialog();
    view->selection_dialog=std::unique_ptr<SelectionDialog>(new SelectionDialog(*view, view->get_buffer()->create_mark(iter), true, true));
    auto rows=std::make_shared<std::unordered_map<std::string, Debug::Variable> >();
    if(variables.size()==0)
      return;
    
    for(auto &variable: variables) {
      std::string row="#"+std::to_string(variable.thread_index_id)+":#"+std::to_string(variable.frame_index)+":"+variable.file_path.filename().string()+":"+std::to_string(variable.line_nr)+" - <b>"+Glib::Markup::escape_text(variable.name)+"</b>";
      
      (*rows)[row]=variable;
      view->selection_dialog->add_row(row);
    }
    
    view->selection_dialog->on_select=[this, rows](const std::string& selected, bool hide_window) {
      auto variable=rows->at(selected);
      if(!variable.file_path.empty()) {
        notebook.open(variable.file_path);
        if(notebook.get_current_page()!=-1) {
          auto view=notebook.get_current_view();
          
          Debug::get().select_frame(variable.frame_index, variable.thread_index_id);
          
          view->get_buffer()->place_cursor(view->get_buffer()->get_iter_at_line_index(variable.line_nr-1, variable.line_index-1));
          
          while(g_main_context_pending(NULL))
            g_main_context_iteration(NULL, false);
          if(notebook.get_current_page()!=-1 && notebook.get_current_view()==view)
            view->scroll_to(view->get_buffer()->get_insert(), 0.0, 1.0, 0.5);
        }
      }
    };
    
    view->selection_dialog->on_hide=[this]() {
      debug_variable_tooltips.hide();
      debug_variable_tooltips.clear();
    };
    
    view->selection_dialog->on_changed=[this, rows, iter](const std::string &selected) {
      if(selected.empty()) {
        debug_variable_tooltips.hide();
        return;
      }
      if(notebook.get_current_page()!=-1) {
        auto view=notebook.get_current_view();
        debug_variable_tooltips.clear();
        auto create_tooltip_buffer=[this, rows, view, selected]() {
          auto variable=rows->at(selected);
          auto tooltip_buffer=Gtk::TextBuffer::create(view->get_buffer()->get_tag_table());
          
          Glib::ustring value=variable.value;
          if(!value.empty()) {
            Glib::ustring::iterator iter;
            while(!value.validate(iter)) {
              auto next_char_iter=iter;
              next_char_iter++;
              value.replace(iter, next_char_iter, "?");
            } 
            tooltip_buffer->insert_with_tag(tooltip_buffer->get_insert()->get_iter(), value.substr(0, value.size()-1), "def:note");
          }
          
          return tooltip_buffer;
        };
        
        debug_variable_tooltips.emplace_back(create_tooltip_buffer, *view, view->get_buffer()->create_mark(iter), view->get_buffer()->create_mark(iter));
    
        debug_variable_tooltips.show(true);
      }
    };
    
    view->selection_dialog->show();
  }
}

void ProjectClang::debug_run_command(const std::string &command) {
  if(debugging) {
    auto command_return=Debug::get().run_command(command);
    Terminal::get().async_print(command_return.first);
    Terminal::get().async_print(command_return.second, true);
  }
}

void ProjectClang::debug_delete() {
  debug_start_mutex.lock();
  Debug::get().delete_debug();
  debug_start_mutex.unlock();
}
#endif

ProjectMarkdown::~ProjectMarkdown() {
  if(!last_temp_path.empty()) {
    boost::filesystem::remove(last_temp_path);
    last_temp_path=boost::filesystem::path();
  }
}

void ProjectMarkdown::compile_and_run() {
  if(!last_temp_path.empty()) {
    boost::filesystem::remove(last_temp_path);
    last_temp_path=boost::filesystem::path();
  }
  
  std::stringstream stdin_stream, stdout_stream;
  auto exit_status=Terminal::get().process(stdin_stream, stdout_stream, "markdown "+notebook.get_current_view()->file_path.string());
  if(exit_status==0) {
    boost::system::error_code ec;
    auto temp_path=boost::filesystem::temp_directory_path(ec);
    if(!ec) {
      temp_path/=boost::filesystem::unique_path();
      temp_path+=".html";
      if(!boost::filesystem::exists(temp_path)) {
        last_temp_path=temp_path;
        std::ofstream file_stream(temp_path.string(), std::fstream::binary);
        file_stream << stdout_stream.rdbuf();
        file_stream.close();
        
        auto uri=temp_path.string();
#ifdef __APPLE__
        Terminal::get().process("open \""+uri+"\"");
#else
#ifdef __linux
        uri="file://"+uri;
#endif
        GError* error=NULL;
        gtk_show_uri(NULL, uri.c_str(), GDK_CURRENT_TIME, &error);
        g_clear_error(&error);
#endif
      }
    }
  }
}

void ProjectPython::compile_and_run() {
  auto command="python "+notebook.get_current_view()->file_path.string();
  Terminal::get().print("Running "+command+"\n");
  Terminal::get().async_process(command, notebook.get_current_view()->file_path.parent_path(), [command](int exit_status) {
    Terminal::get().async_print(command+" returned: "+std::to_string(exit_status)+'\n');
  });
}

void ProjectJavaScript::compile_and_run() {
  auto command="node "+notebook.get_current_view()->file_path.string();
  Terminal::get().print("Running "+command+"\n");
  Terminal::get().async_process(command, notebook.get_current_view()->file_path.parent_path(), [command](int exit_status) {
    Terminal::get().async_print(command+" returned: "+std::to_string(exit_status)+'\n');
  });
}

void ProjectHTML::compile_and_run() {
  auto uri=notebook.get_current_view()->file_path.string();
#ifdef __APPLE__
  Terminal::get().process("open \""+uri+"\"");
#else
#ifdef __linux
  uri="file://"+uri;
#endif
  GError* error=NULL;
  gtk_show_uri(NULL, uri.c_str(), GDK_CURRENT_TIME, &error);
  g_clear_error(&error);
#endif
}