#ifndef TRANSFER_FUNCTION_HPP
#define TRANSFER_FUNCTION_HPP

#include "data_types_fwd.hpp"

#include <string>
#include <map>

#define GLM_FORCE_RADIANS
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

class Transfer_function
{
public:
    struct Vertex
    {
        glm::vec3 position;
        glm::vec3 color;
    };
  typedef std::pair<unsigned, glm::vec4> element_type;
  typedef std::map<unsigned, glm::vec4>  container_type;

public:
  Transfer_function();
  ~Transfer_function() {}

  void add(float, glm::vec4);
  void add(unsigned, glm::vec4);

  void reset();

  image_data_type          get_RGBA_transfer_function_buffer() const;
  void                     update_and_draw();
  const container_type&    get_piecewise_container(){ return m_piecewise_container;};

private:
    void update_vbo();

private:
  container_type    m_piecewise_container;
  
  unsigned int      m_program_id;
  unsigned int      m_vao;
    
  bool              m_dirty;
};

#endif // define TRANSFER_FUNCTION_HPP
