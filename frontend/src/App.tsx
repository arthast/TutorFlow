import { Navigate, Route, Routes } from "react-router-dom";
import { useAuth } from "./auth";
import Login from "./pages/Login";
import Register from "./pages/Register";
import Teacher from "./pages/Teacher";
import Student from "./pages/Student";
import TeacherAssignmentReview from "./pages/TeacherAssignmentReview";
import TeacherLessons from "./pages/TeacherLessons";
import TeacherStudentCard from "./pages/TeacherStudentCard";
import ChatPage from "./pages/ChatPage";

export default function App() {
  const { user, role, loading } = useAuth();

  if (loading) {
    return <div className="container">Загрузка…</div>;
  }

  return (
    <Routes>
      <Route
        path="/login"
        element={user ? <Navigate to="/" replace /> : <Login />}
      />
      <Route
        path="/register"
        element={user ? <Navigate to="/" replace /> : <Register />}
      />
      <Route
        path="/teacher"
        element={role === "teacher" ? <Teacher /> : <Navigate to="/" replace />}
      />
      <Route
        path="/teacher/lessons"
        element={role === "teacher" ? <TeacherLessons /> : <Navigate to="/" replace />}
      />
      <Route
        path="/teacher/students/:studentId"
        element={role === "teacher" ? <TeacherStudentCard /> : <Navigate to="/" replace />}
      />
      <Route
        path="/teacher/assignments/:assignmentId/review"
        element={role === "teacher" ? <TeacherAssignmentReview /> : <Navigate to="/" replace />}
      />
      <Route
        path="/teacher/chat"
        element={role === "teacher" ? <ChatPage /> : <Navigate to="/" replace />}
      />
      <Route
        path="/student"
        element={role === "student" ? <Student /> : <Navigate to="/" replace />}
      />
      <Route
        path="/student/chat"
        element={role === "student" ? <ChatPage /> : <Navigate to="/" replace />}
      />
      <Route
        path="*"
        element={
          <Navigate
            to={!user ? "/login" : role === "teacher" ? "/teacher" : "/student"}
            replace
          />
        }
      />
    </Routes>
  );
}
