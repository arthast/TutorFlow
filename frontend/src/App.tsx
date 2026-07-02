import { Navigate, Route, Routes } from "react-router-dom";
import { useAuth } from "./auth";
import { RealtimeToasts } from "./ui";
import Login from "./pages/Login";
import Register from "./pages/Register";
import Teacher from "./pages/Teacher";
import Student from "./pages/Student";
import TeacherAssignmentReview from "./pages/TeacherAssignmentReview";
import TeacherLessons from "./pages/TeacherLessons";
import TeacherStudentCard from "./pages/TeacherStudentCard";
import ChatPage from "./pages/ChatPage";
import TeacherStudents from "./pages/TeacherStudents";
import TeacherAssignments from "./pages/TeacherAssignments";
import TeacherFinance from "./pages/TeacherFinance";
import TeacherReceipts from "./pages/TeacherReceipts";
import TeacherSettings from "./pages/TeacherSettings";
import StudentLessons from "./pages/StudentLessons";
import StudentAssignments from "./pages/StudentAssignments";
import StudentPayments from "./pages/StudentPayments";
import StudentReceipts from "./pages/StudentReceipts";
import StudentSettings from "./pages/StudentSettings";

export default function App() {
  const { user, role, loading } = useAuth();

  if (loading) {
    return <div className="container">Загрузка…</div>;
  }

  return (
    <>
    {user && <RealtimeToasts />}
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
        path="/teacher/students"
        element={role === "teacher" ? <TeacherStudents /> : <Navigate to="/" replace />}
      />
      <Route
        path="/teacher/students/:studentId"
        element={role === "teacher" ? <TeacherStudentCard /> : <Navigate to="/" replace />}
      />
      <Route
        path="/teacher/assignments"
        element={role === "teacher" ? <TeacherAssignments /> : <Navigate to="/" replace />}
      />
      <Route
        path="/teacher/assignments/:assignmentId/review"
        element={role === "teacher" ? <TeacherAssignmentReview /> : <Navigate to="/" replace />}
      />
      <Route
        path="/teacher/finance"
        element={role === "teacher" ? <TeacherFinance /> : <Navigate to="/" replace />}
      />
      <Route
        path="/teacher/receipts"
        element={role === "teacher" ? <TeacherReceipts /> : <Navigate to="/" replace />}
      />
      <Route
        path="/teacher/chat"
        element={role === "teacher" ? <ChatPage /> : <Navigate to="/" replace />}
      />
      <Route
        path="/teacher/settings"
        element={role === "teacher" ? <TeacherSettings /> : <Navigate to="/" replace />}
      />
      <Route
        path="/student"
        element={role === "student" ? <Student /> : <Navigate to="/" replace />}
      />
      <Route
        path="/student/lessons"
        element={role === "student" ? <StudentLessons /> : <Navigate to="/" replace />}
      />
      <Route
        path="/student/assignments"
        element={role === "student" ? <StudentAssignments /> : <Navigate to="/" replace />}
      />
      <Route
        path="/student/payments"
        element={role === "student" ? <StudentPayments /> : <Navigate to="/" replace />}
      />
      <Route
        path="/student/receipts"
        element={role === "student" ? <StudentReceipts /> : <Navigate to="/" replace />}
      />
      <Route
        path="/student/chat"
        element={role === "student" ? <ChatPage /> : <Navigate to="/" replace />}
      />
      <Route
        path="/student/settings"
        element={role === "student" ? <StudentSettings /> : <Navigate to="/" replace />}
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
    </>
  );
}
