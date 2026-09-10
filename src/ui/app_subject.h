/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <src/core/lv_observer.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ui {

class SubjectBase {
 public:
  SubjectBase(const SubjectBase&)            = delete;
  SubjectBase& operator=(const SubjectBase&) = delete;

  virtual ~SubjectBase() { deinit_if_initialized_(); }

  lv_subject_t* subject() { return &subject_; }
  const lv_subject_t* subject() const { return &subject_; }

 protected:
  SubjectBase() = default;

  void deinit_if_initialized_() {
    if (subject_.type != LV_SUBJECT_TYPE_INVALID) {
      lv_subject_deinit(&subject_);
    }
  }

  lv_subject_t subject_{};
};

class SubjectInt : public SubjectBase {
 public:
  explicit SubjectInt(int32_t value = 0) { lv_subject_init_int(&subject_, value); }

  void set(int32_t value) { lv_subject_set_int(&subject_, value); }

  int32_t get() const { return lv_subject_get_int(const_cast<lv_subject_t*>(&subject_)); }

  int32_t previous() const {
    return lv_subject_get_previous_int(const_cast<lv_subject_t*>(&subject_));
  }

  void set_min(int32_t value) { lv_subject_set_min_value_int(&subject_, value); }

  void set_max(int32_t value) { lv_subject_set_max_value_int(&subject_, value); }
};

class SubjectBool : public SubjectInt {
 public:
  explicit SubjectBool(bool value = false)
      : SubjectInt(value ? 1 : 0) {}

  void set(bool value) { SubjectInt::set(value ? 1 : 0); }

  bool get() const { return SubjectInt::get() != 0; }

  bool previous() const { return SubjectInt::previous() != 0; }
};

#if LV_USE_FLOAT
class SubjectFloat : public SubjectBase {
 public:
  explicit SubjectFloat(float value = 0.0f) { lv_subject_init_float(&subject_, value); }

  void set(float value) { lv_subject_set_float(&subject_, value); }

  float get() const { return lv_subject_get_float(const_cast<lv_subject_t*>(&subject_)); }

  float previous() const {
    return lv_subject_get_previous_float(const_cast<lv_subject_t*>(&subject_));
  }

  void set_min(float value) { lv_subject_set_min_value_float(&subject_, value); }

  void set_max(float value) { lv_subject_set_max_value_float(&subject_, value); }
};
#endif

template <size_t Size>
class SubjectString : public SubjectBase {
 public:
  explicit SubjectString(const char* value = "") {
    lv_subject_init_string(&subject_,
                           buffer_.data(),
                           previous_buffer_.data(),
                           Size,
                           value ? value : "");
  }

  void set(const std::string& value) { set(value.c_str()); }

  void set(const char* value) { lv_subject_copy_string(&subject_, value ? value : ""); }

  template <typename... Args>
  void format(const char* fmt, Args... args) {
    lv_subject_snprintf(&subject_, fmt ? fmt : "", args...);
  }

  const char* get() const { return lv_subject_get_string(const_cast<lv_subject_t*>(&subject_)); }

  const char* previous() const {
    return lv_subject_get_previous_string(const_cast<lv_subject_t*>(&subject_));
  }

 private:
  std::array<char, Size> buffer_{};
  std::array<char, Size> previous_buffer_{};
};

class SubjectPointer : public SubjectBase {
 public:
  explicit SubjectPointer(void* value = nullptr) { lv_subject_init_pointer(&subject_, value); }

  void set(void* value) { lv_subject_set_pointer(&subject_, value); }

  const void* get() const { return lv_subject_get_pointer(const_cast<lv_subject_t*>(&subject_)); }

  const void* previous() const {
    return lv_subject_get_previous_pointer(const_cast<lv_subject_t*>(&subject_));
  }
};

class SubjectColor : public SubjectBase {
 public:
  explicit SubjectColor(lv_color_t value = lv_color_black()) {
    lv_subject_init_color(&subject_, value);
  }

  void set(lv_color_t value) { lv_subject_set_color(&subject_, value); }

  lv_color_t get() const { return lv_subject_get_color(const_cast<lv_subject_t*>(&subject_)); }

  lv_color_t previous() const {
    return lv_subject_get_previous_color(const_cast<lv_subject_t*>(&subject_));
  }
};

class SubjectGroup : public SubjectBase {
 public:
  SubjectGroup() = default;

  explicit SubjectGroup(const std::vector<lv_subject_t*>& subjects) { init(subjects); }

  explicit SubjectGroup(const std::vector<SubjectBase*>& subjects) { init(subjects); }

  void init(const std::vector<lv_subject_t*>& subjects) {
    deinit_if_initialized_();
    members_ = subjects;
    lv_subject_init_group(&subject_, members_.data(), static_cast<uint32_t>(members_.size()));
  }

  void init(const std::vector<SubjectBase*>& subjects) {
    deinit_if_initialized_();
    members_.clear();
    members_.reserve(subjects.size());
    for (SubjectBase* item : subjects) {
      if (item) {
        members_.push_back(item->subject());
      }
    }
    lv_subject_init_group(&subject_, members_.data(), static_cast<uint32_t>(members_.size()));
  }

  lv_subject_t* element(int32_t index) { return lv_subject_get_group_element(&subject_, index); }

 private:
  std::vector<lv_subject_t*> members_;
};

}  // namespace ui
